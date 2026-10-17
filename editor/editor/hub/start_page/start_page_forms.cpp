#include "start_page.h"
#include "start_page_widgets.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/screen_card.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/system/project_manager.h>
#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <logging/logging.h>

namespace unravel
{
namespace widgets = start_page_widgets;

namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float FORMS_VIEW_SECTION_GAP = 1.3f;
constexpr float FORMS_VIEW_FIELD_GAP = 0.4f;
constexpr float FORMS_VIEW_WIDTH = 40.0f;
constexpr float FORMS_VIEW_REMOVE_WIDTH = 56.0f;
constexpr ImU32 FORMS_VIEW_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
/// How long the toast of a failed folder delete stays up.
constexpr int FORMS_VIEW_ERROR_TOAST_MILLISECONDS = 4000;

void draw_form_label(const char* label)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(FORMS_VIEW_FIELD_GAP)));
}

void draw_form_gap()
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(FORMS_VIEW_SECTION_GAP)));
}
} // namespace

auto start_page::draw_sub_page_header(const char* title, const char* subtitle) -> bool
{
    const bool is_back_pressed = widgets::draw_button(ICON_MDI_ARROW_LEFT "  Back", widgets::button_kind::ghost);
    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive();
    draw_form_gap();
    widgets::draw_page_title(title, subtitle);
    draw_form_gap();
    return is_back_pressed || is_escape_pressed;
}

void start_page::draw_create_project_view(rtti::context& ctx)
{
    if(draw_sub_page_header("New Project", "Pick a name and the folder the project gets created in."))
    {
        show_view(view_state::projects);
        return;
    }
    const float form_width = ImMin(to_pixels(FORMS_VIEW_WIDTH), ImGui::GetContentRegionAvail().x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    draw_form_label("Project Name");
    widgets::push_field_style();
    ImGui::SetNextItemWidth(form_width);
    ImGui::InputTextWidget("##project_name", new_project_name_, false);
    widgets::pop_field_style();
    draw_form_gap();
    draw_form_label("Location");
    const float browse_size = widgets::get_button_height();
    widgets::push_field_style();
    ImGui::SetNextItemWidth(form_width - browse_size - spacing);
    ImGui::InputTextWidget("##project_directory", new_project_directory_, false);
    widgets::pop_field_style();
    ImGui::SameLine();
    if(widgets::draw_icon_toggle("##pick_directory", ICON_MDI_FOLDER_OPEN_OUTLINE, true, "Browse for a folder..."))
    {
        std::string picked_directory;
        if(native::pick_folder_dialog(picked_directory))
        {
            new_project_directory_ = picked_directory;
        }
    }
    const bool is_complete = !new_project_name_.empty() && !new_project_directory_.empty();
    const fs::path project_path = fs::path(new_project_directory_) / new_project_name_;
    // A folder with content in it would get a project poured over whatever it holds.
    fs::error_code error;
    const bool is_occupied = is_complete && fs::exists(project_path, error) && !fs::is_empty(project_path, error);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(FORMS_VIEW_FIELD_GAP)));
    if(is_occupied)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FORMS_VIEW_WARNING_COLOR),
                           "%s",
                           ICON_MDI_ALERT_OUTLINE " That folder already exists and is not empty.");
    }
    else
    {
        const std::string preview = is_complete ? fmt::format("The project will be created at {}", project_path.generic_string())
                                                : std::string("The project gets a folder of its own inside the location.");
        widgets::draw_muted_text(preview.c_str(), form_width);
    }
    draw_form_gap();
    if(widgets::draw_button(ICON_MDI_PLUS "  Create Project", widgets::button_kind::primary, 0.0f, is_complete && !is_occupied))
    {
        auto& pm = ctx.get_cached<project_manager>();
        auto& em = ctx.get_cached<editing_manager>();
        const fs::path created_path = fs::path(project_path).make_preferred();
        em.queue_action<untracked_editor_state_action_t>("Create Project",
                        [&ctx, &pm, created_path]()
                        {
                            pm.create_project(ctx, created_path);
                        });
        show_view(view_state::projects);
        return;
    }
    ImGui::SameLine();
    if(widgets::draw_button("Cancel", widgets::button_kind::secondary))
    {
        show_view(view_state::projects);
    }
}

void start_page::draw_remove_project_view(rtti::context& ctx)
{
    const project_entry* entry = find_project(project_to_remove_);
    const bool wants_back = draw_sub_page_header("Remove Project", "Take the project off the list, or delete it from the disk as well.");
    if(wants_back || entry == nullptr)
    {
        show_view(view_state::projects);
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    const float form_width = ImMin(to_pixels(FORMS_VIEW_REMOVE_WIDTH), ImGui::GetContentRegionAvail().x);
    ImGui::BeginChild("##project_to_remove", ImVec2(form_width, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    item_state state{};
    state.is_interactive = false;
    draw_project_row(*entry, state);
    ImGui::EndChild();
    draw_form_gap();
    // A copy: both removals re-read the entries and leave the pointer dangling.
    const std::string path = entry->path;
    if(widgets::draw_button(ICON_MDI_PLAYLIST_REMOVE "  Remove from Recents", widgets::button_kind::primary))
    {
        remove_from_recents(pm, path);
        show_view(view_state::projects);
        return;
    }
    ImGui::SetItemTooltipEx("%s", "Take it off the list. The project folder stays untouched.");
    ImGui::SameLine();
    if(widgets::draw_button(ICON_MDI_DELETE_OUTLINE "  Delete Folder", widgets::button_kind::danger))
    {
        // A folder that could not be deleted (a file in use) stays on the list, so it can be found again.
        fs::error_code error;
        fs::remove_all(fs::path(path), error);
        if(error)
        {
            APPLOG_ERROR("Could not delete the project folder {}: {}", path, error.message());
            ImGui::PushNotification(ImGuiToast(ImGuiToastType_Error, FORMS_VIEW_ERROR_TOAST_MILLISECONDS, "Could not delete the project folder."));
        }
        else
        {
            remove_from_recents(pm, path);
        }
        show_view(view_state::projects);
        return;
    }
    ImGui::SetItemTooltipEx("%s", "DANGER: permanently deletes the project folder and everything in it.");
    ImGui::SameLine();
    if(widgets::draw_button("Cancel", widgets::button_kind::secondary))
    {
        show_view(view_state::projects);
        return;
    }
    draw_form_gap();
    widgets::draw_muted_text("Removing from the recents is the safe choice: the files stay where they are and the project can be "
                             "opened again with Open...",
                             form_width);
}
} // namespace unravel
