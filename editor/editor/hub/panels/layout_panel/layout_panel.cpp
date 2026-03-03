#include "layout_panel.h"
#include "../panel.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"

#include <editor/imgui/integration/imgui_messagebox.h>
#include <imgui/imgui.h>

namespace unravel
{
namespace
{
auto filename_char_filter(ImGuiInputTextCallbackData* data) -> int
{
    auto c = data->EventChar;
    if(c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
    {
        return 1;
    }
    return 0;
}
} // namespace

layout_panel::layout_panel(imgui_panels* parent, const char* name) : panel_base(name), parent_(parent)
{
}

auto layout_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return 0;
}

void layout_panel::draw_ui(rtti::context& ctx)
{
    (void)ctx;

    auto& lm = parent_->get_layout_manager();

    if(ImGui::Button(ICON_MDI_PLUS " Create Layout", ImVec2(-1, 0)))
    {
        open_create_popup_ = true;
        create_name_buf_.fill('\0');
    }

    if(ImGui::Button(ICON_MDI_RESTORE " Reset to Default", ImVec2(-1, 0)))
    {
        lm.reset_to_default();
    }

    ImGui::Separator();

    draw_presets_list();

    ImGui::Separator();

    if(ImGui::Button(ICON_MDI_FOLDER_OPEN " Open in Explorer", ImVec2(-1, 0)))
    {
        fs::show_in_graphical_env(lm.get_layouts_directory());
    }

    draw_create_popup();
}

void layout_panel::draw_create_popup()
{
    if(open_create_popup_)
    {
        ImGui::OpenPopup("Create Layout");
        open_create_popup_ = false;
    }

    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);

    if(!ImGui::BeginPopupModal("Create Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    auto& lm = parent_->get_layout_manager();

    ImGui::TextWrapped("Enter the name of the layout you want to create");
    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Layout Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    bool enter_pressed = ImGui::InputText("##create_name",
                                          create_name_buf_.data(),
                                          create_name_buf_.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                                          filename_char_filter);

    bool name_empty = (create_name_buf_[0] == '\0');
    bool already_exists = !name_empty && lm.has_preset(create_name_buf_.data());

    if(already_exists)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), ICON_MDI_ALERT " A preset with this name already exists and will be overwritten.");
    }

    ImGui::Spacing();

    float button_width = 120.0f;
    float total_width = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - total_width + ImGui::GetCursorPosX());

    if(ImGui::Button("Create", ImVec2(button_width, 0)) || (enter_pressed && !name_empty))
    {
        if(!name_empty)
        {
            lm.save_preset(create_name_buf_.data());
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(button_width, 0)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void layout_panel::draw_presets_list()
{
    auto& lm = parent_->get_layout_manager();
    auto presets = lm.get_preset_names();

    ImGui::Text("Saved Layouts:");

    if(presets.empty())
    {
        ImGui::TextDisabled("No saved layouts yet.");
        return;
    }

    for(const auto& name : presets)
    {
        ImGui::PushID(name.c_str());

        float icon_btn_width = ImGui::CalcTextSize(ICON_MDI_DELETE).x + ImGui::GetStyle().FramePadding.x * 2;
        float apply_width = ImGui::GetContentRegionAvail().x - icon_btn_width * 2 - ImGui::GetStyle().ItemSpacing.x * 2;

        if(ImGui::Button(name.c_str(), ImVec2(apply_width, 0)))
        {
            lm.load_preset(name);
        }
        ImGui::SetItemTooltipEx("Click to apply \"%s\"", name.c_str());

        ImGui::SameLine();

        if(ImGui::Button(ICON_MDI_UPDATE, ImVec2(icon_btn_width, 0)))
        {
            lm.save_preset(name);
        }
        ImGui::SetItemTooltipEx("Update \"%s\" with current layout", name.c_str());

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
        if(ImGui::Button(ICON_MDI_DELETE, ImVec2(icon_btn_width, 0)))
        {
            auto preset_name = name;
            auto* panels = parent_;
            ImBox::ShowDeleteConfirmation(
                "Delete Layout?",
                fmt::format("Delete layout preset \"{}\"?\n\nThis cannot be undone.", preset_name),
                [panels, preset_name](ImBox::ModalResult result) -> void
                {
                    if(result == ImBox::ModalResult::Delete)
                    {
                        panels->get_layout_manager().delete_preset(preset_name);
                    }
                });
        }
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltipEx("Delete \"%s\"", name.c_str());

        ImGui::PopID();
    }
}

} // namespace unravel
