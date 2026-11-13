#include "package_import_view.h"
#include "packages_widgets.h"

#include "../deploy_panel/deploy_panel_widgets.h"
#include "../panel_section.h"
#include "../panel_toolbar.h"

#include <editor/assets/package_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_style.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/prefab.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

namespace unravel
{
// A unity build merges the anonymous namespaces of the editor's sources; the view's own namespace keeps its
// helper names from clashing.
namespace package_import_window
{
namespace
{
namespace widgets = packages_widgets;
using packages::file_action;
using packages::file_status;

constexpr float STATUS_COLUMN = 8.0f;
constexpr float ACTION_COLUMN = 9.5f;
constexpr float ROW_PADDING_X = 0.45f;
constexpr float SEARCH_WIDTH = 12.0f;
constexpr float SEARCH_MIN_WIDTH = 5.0f;
constexpr float HEADER_TILE_SIZE = 3.4f;
constexpr float HEADER_TILE_GAP = 0.85f;
constexpr float LIST_MIN_HEIGHT = 8.0f;
constexpr float README_MAX_HEIGHT = 14.0f;
constexpr int COUNTER_COUNT = 5;

/// The groups the file list filters by.
struct status_group
{
    const char* name;
    ImU32 color;
};

constexpr status_group STATUS_GROUPS[] = {
    {"New", widgets::COLOR_NEW},
    {"Updates", widgets::COLOR_UPDATE},
    {"Edited by You", widgets::COLOR_EDITED},
    {"Clashes", widgets::COLOR_CLASH},
    {"Other", widgets::COLOR_MUTED_PILL},
};

auto get_group(file_status status) -> std::size_t
{
    switch(status)
    {
        case file_status::added:
            return 0;
        case file_status::updated:
            return 1;
        case file_status::edited:
        case file_status::deleted:
            return 2;
        case file_status::uid_clash:
        case file_status::path_clash:
            return 3;
        default:
            return 4;
    }
}

auto get_status_color(file_status status) -> ImU32
{
    switch(status)
    {
        case file_status::added:
            return widgets::COLOR_NEW;
        case file_status::updated:
            return widgets::COLOR_UPDATE;
        case file_status::edited:
        case file_status::deleted:
            return widgets::COLOR_EDITED;
        case file_status::uid_clash:
        case file_status::path_clash:
            return widgets::COLOR_CLASH;
        case file_status::incompatible:
            return widgets::COLOR_ERROR;
        default:
            return widgets::COLOR_MUTED_PILL;
    }
}

auto describe_status(const packages::planned_file& planned) -> std::string
{
    switch(planned.status)
    {
        case file_status::added:
            return "The project does not have this file yet.";
        case file_status::updated:
            return "An earlier version installed this file, unchanged since. The package brings another version of it.";
        case file_status::unchanged:
            return "The project's file is the same as the package's.";
        case file_status::edited:
            return "An earlier version installed this file and it was edited since, at " + planned.existing_key + ".";
        case file_status::deleted:
            return "An earlier version installed this file and it was deleted since.";
        case file_status::present:
            return "The project already holds this file, with the same id, at " + planned.existing_key + ".";
        case file_status::uid_clash:
            return "Another file of the project has this file's id: " + planned.existing_key + ".";
        case file_status::path_clash:
            return "Another file of the project is where this one goes.";
        case file_status::incompatible:
            return "It is in a scene or prefab layout this engine does not read, so it stays out.";
    }
    return {};
}

/// Where the file ends up, for the row's tooltip.
auto describe_destination(const packages::planned_file& planned) -> std::string
{
    switch(planned.action)
    {
        case file_action::write:
            return "Written to " + planned.target_key + ".";
        case file_action::write_copy:
            return "Added as " + planned.copy_key + ".";
        case file_action::replace:
            return "Written to " + planned.existing_key + ".";
        default:
            return {};
    }
}

auto get_package_size_text(const packages::package_archive& archive) -> std::string
{
    return format_bytes(archive.get_file_size(), 1);
}
} // namespace
} // namespace package_import_window

using namespace package_import_window;

void package_import_view::open(rtti::context& ctx, const fs::path& package_file)
{
    close();
    is_open_ = true;
    package_file_ = package_file;
    auto archive = std::make_shared<packages::package_archive>();
    if(!archive->open(package_file, open_error_))
    {
        return;
    }
    archive_ = std::move(archive);
    readme_ = archive_->read_document(packages::PACKAGE_README_NAME);
    start_planning(ctx);
}

void package_import_view::close()
{
    is_open_ = false;
    open_error_.clear();
    archive_.reset();
    project_ = {};
    plan_job_ = {};
    has_plan_ = false;
    plan_ = {};
    next_plan_problems_.clear();
    apply_job_ = {};
    is_applying_ = false;
    readme_.clear();
    has_result_ = false;
    result_ = {};
    search_.Clear();
    shown_groups_.fill(true);
}

auto package_import_view::get_package_name() const -> const std::string&
{
    return plan_.manifest.name;
}

void package_import_view::start_planning(rtti::context& ctx)
{
    has_plan_ = false;
    project_ = package_actions::make_project_view(ctx);
    plan_job_ = ctx.get_cached<threader>()
                    .pool
                    ->schedule("Planning Package Import",
                               [archive = archive_, project = project_]()
                               {
                                   return packages::plan_import(*archive, project);
                               })
                    .share();
}

void package_import_view::poll_jobs(rtti::context& ctx)
{
    if(!has_plan_ && plan_job_.valid() && plan_job_.is_ready())
    {
        plan_ = plan_job_.get();
        plan_job_ = {};
        plan_.problems.insert(plan_.problems.begin(), next_plan_problems_.begin(), next_plan_problems_.end());
        next_plan_problems_.clear();
        has_plan_ = true;
    }
    if(is_applying_ && apply_job_.valid() && apply_job_.is_ready())
    {
        auto outcome = apply_job_.get();
        apply_job_ = {};
        is_applying_ = false;
        finish_apply(ctx, std::move(outcome));
    }
}

void package_import_view::finish_apply(rtti::context& ctx, apply_outcome outcome)
{
    project_ = std::move(outcome.project);
    if(outcome.is_plan_changed)
    {
        plan_ = std::move(outcome.plan);
        plan_.warnings.insert(plan_.warnings.begin(),
                              "The project changed while the review was open, and nothing was imported. The plan is made "
                              "again: check it, then import.");
        return;
    }
    result_ = std::move(outcome.result);
    if(result_.is_done)
    {
        plan_ = std::move(outcome.plan);
        has_result_ = true;
        return;
    }
    // The plan shown is made again from the project as it is now, with why the import did not go through.
    next_plan_problems_ = {"The import failed and nothing changed: " + result_.error};
    start_planning(ctx);
}

auto package_import_view::draw(rtti::context& ctx) -> bool
{
    poll_jobs(ctx);
    if(!open_error_.empty())
    {
        draw_open_error();
        return draw_footer(ctx);
    }
    if(has_result_)
    {
        return draw_result(ctx);
    }
    if(!has_plan_)
    {
        draw_progress("Reading the package", "The package's files are compared with the project's.");
        return draw_footer(ctx);
    }
    draw_header();
    if(is_applying_)
    {
        const auto text = fmt::format("{} is written into the project. The review closes on its own once it is done.",
                                      plan_.manifest.get_title());
        draw_progress("Importing", text);
        return draw_footer(ctx);
    }
    draw_notes();
    draw_counters();
    draw_files();
    draw_readme();
    return draw_footer(ctx);
}

void package_import_view::draw_progress(const char* title, const std::string& text)
{
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    if(widgets::begin_card("##import_progress"))
    {
        widgets::draw_title(title);
        deploy_widgets::draw_icon_line(ICON_MDI_PROGRESS_CLOCK, imgui_style::get_muted_text_color_u32(), text.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    widgets::end_card();
}

void package_import_view::draw_open_error()
{
    if(widgets::begin_card("##import_error"))
    {
        widgets::draw_title("This package cannot be opened");
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, widgets::COLOR_ERROR, open_error_.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        widgets::draw_muted_wrapped(package_file_.generic_string().c_str());
    }
    widgets::end_card();
}

void package_import_view::draw_header()
{
    const auto& manifest = plan_.manifest;
    if(widgets::begin_card("##import_header"))
    {
        const float tile_size = widgets::to_pixels(HEADER_TILE_SIZE);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        widgets::draw_package_tile(ImGui::GetWindowDrawList(), origin, tile_size, manifest.name);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tile_size + widgets::to_pixels(HEADER_TILE_GAP), origin.y));
        ImGui::BeginGroup();
        const std::string title = plan_.is_update() && plan_.installed->version != manifest.version
                                      ? fmt::format("Update {} {} to {}", manifest.get_title(), plan_.installed->version, manifest.version)
                                      : fmt::format("Import {} {}", manifest.get_title(), manifest.version);
        widgets::draw_title(title.c_str());
        std::string byline = manifest.name;
        if(!manifest.author.empty())
        {
            byline += "   by " + manifest.author;
        }
        byline += "   " + get_package_size_text(*archive_) + "   made with engine " + manifest.exported_with;
        widgets::draw_muted_wrapped(byline.c_str());
        if(!manifest.description.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(manifest.description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, ImMax(ImGui::GetCursorScreenPos().y, origin.y + tile_size)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    widgets::end_card();
}

void package_import_view::draw_notes()
{
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const bool has_scripts = !plan_.manifest.script_types.empty() ||
                             std::any_of(plan_.manifest.files.begin(),
                                         plan_.manifest.files.end(),
                                         [](const packages::package_file& file)
                                         {
                                             return file.type == ".cs";
                                         });
    if(plan_.problems.empty() && plan_.clashes.empty() && plan_.warnings.empty() && !has_scripts)
    {
        return;
    }
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    for(const auto* sentences : {&plan_.problems, &plan_.clashes})
    {
        for(const auto& problem : *sentences)
        {
            deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, widgets::COLOR_ERROR, problem.c_str(), text_color);
        }
    }
    for(const auto& warning : plan_.warnings)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT, widgets::COLOR_EDITED, warning.c_str(), text_color);
    }
    if(has_scripts)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_LANGUAGE_CSHARP,
                                       imgui_style::get_muted_text_color_u32(),
                                       "It holds C# scripts. Scripts run in the editor, so import packages from people you trust.",
                                       imgui_style::get_muted_text_color_u32());
    }
}

void package_import_view::draw_counters()
{
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    const float gap = ImGui::GetFontSize() * 0.5f;
    const float width = widgets::calc_tile_width(COUNTER_COUNT);
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const auto count_color = [text_color](std::size_t count, ImU32 color)
    {
        return count > 0 ? color : text_color;
    };
    const std::size_t added = plan_.count(file_status::added);
    const std::size_t updated = plan_.count(file_status::updated);
    const std::size_t edited = plan_.count(file_status::edited) + plan_.count(file_status::deleted);
    const std::size_t clashes = plan_.count(file_status::uid_clash) + plan_.count(file_status::path_clash);
    const std::size_t removed = std::size_t(std::count_if(plan_.removals.begin(),
                                                          plan_.removals.end(),
                                                          [](const packages::planned_removal& removal)
                                                          {
                                                              return removal.is_removed;
                                                          }));
    widgets::draw_tile("New Files", std::to_string(added), count_color(added, widgets::COLOR_NEW), width);
    ImGui::SameLine(0.0f, gap);
    widgets::draw_tile("Updates", std::to_string(updated), count_color(updated, widgets::COLOR_UPDATE), width);
    ImGui::SameLine(0.0f, gap);
    widgets::draw_tile("Edited by You", std::to_string(edited), count_color(edited, widgets::COLOR_EDITED), width);
    ImGui::SameLine(0.0f, gap);
    widgets::draw_tile("Clashes", std::to_string(clashes), count_color(clashes, widgets::COLOR_CLASH), width);
    ImGui::SameLine(0.0f, gap);
    widgets::draw_tile("To the Trash", std::to_string(removed), count_color(removed, widgets::COLOR_ERROR), width);
}

void package_import_view::draw_files()
{
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    const std::string title = fmt::format("Files ({})", plan_.files.size());
    if(!panel_section::draw_header("##import_files", ICON_MDI_FILE_TREE, title.c_str(), true).is_open)
    {
        return;
    }
    draw_file_filters();
    std::vector<std::size_t> shown;
    for(std::size_t index = 0; index < plan_.files.size(); ++index)
    {
        if(is_file_shown(index))
        {
            shown.push_back(index);
        }
    }
    const float row_height = ImGui::GetFrameHeight();
    const std::size_t row_count = shown.size() + plan_.removals.size();
    const float wanted = row_height * float(ImMax<std::size_t>(row_count, 1)) + ImGui::GetStyle().WindowPadding.y;
    const float reserved = ImGui::GetFrameHeightWithSpacing() * 3.0f;
    const float height = ImClamp(wanted,
                                 widgets::to_pixels(LIST_MIN_HEIGHT),
                                 ImMax(widgets::to_pixels(LIST_MIN_HEIGHT), ImGui::GetContentRegionAvail().y - reserved));
    if(ImGui::BeginChild("##import_file_list", ImVec2(0.0f, height)))
    {
        if(shown.empty() && plan_.removals.empty())
        {
            widgets::draw_centered_hint("Nothing to show", "No file passes the search and the filters.");
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGuiListClipper clipper;
        clipper.Begin(int(row_count), row_height);
        while(clipper.Step())
        {
            for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                if(std::size_t(row) < shown.size())
                {
                    draw_file_row(shown[std::size_t(row)], row);
                }
                else
                {
                    draw_removal_row(std::size_t(row) - shown.size(), row);
                }
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void package_import_view::draw_file_filters()
{
    if(panel_toolbar::begin_strip("##import_file_filters"))
    {
        const float font_size = ImGui::GetFontSize();
        panel_toolbar::search_field(search_,
                                    ICON_MDI_MAGNIFY " Search files...",
                                    "Show the files whose path holds the text.",
                                    font_size * SEARCH_MIN_WIDTH,
                                    font_size * SEARCH_WIDTH);
        panel_toolbar::align_right();
        std::array<std::size_t, FILTER_COUNT> counts{};
        for(const auto& planned : plan_.files)
        {
            ++counts[get_group(planned.status)];
        }
        for(std::size_t group = 0; group < FILTER_COUNT; ++group)
        {
            const std::string id = fmt::format("##group_{}", group);
            const std::string text = fmt::format("{} {}", STATUS_GROUPS[group].name, counts[group]);
            const std::string tooltip = fmt::format("Show or hide the {} files.", STATUS_GROUPS[group].name);
            if(panel_toolbar::filter_toggle(id.c_str(), text.c_str(), shown_groups_[group], STATUS_GROUPS[group].color, tooltip.c_str()))
            {
                shown_groups_[group] = !shown_groups_[group];
            }
        }
    }
    panel_toolbar::end_strip();
}

void package_import_view::draw_file_row(std::size_t index, int row_index)
{
    auto& planned = plan_.files[index];
    const auto& file = plan_.manifest.files[planned.index];
    const float font_size = ImGui::GetFontSize();
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_width = ImGui::GetContentRegionAvail().x;
    const ImRect rect(row_min, ImVec2(row_min.x + row_width, row_min.y + ImGui::GetFrameHeight()));
    ImGui::PushID(int(index));
    const bool is_hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max) && ImGui::IsWindowHovered();
    widgets::draw_row_background(rect, row_index, is_hovered, false);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = font_size * ROW_PADDING_X;
    const char* status_name = packages::get_file_status_name(planned.status);
    const ImVec2 pill_size = widgets::calc_pill_size(status_name);
    widgets::draw_pill(draw_list,
                       ImVec2(rect.Min.x + padding, rect.Min.y + (rect.GetHeight() - pill_size.y) * 0.5f),
                       status_name,
                       get_status_color(planned.status));
    const float action_width = font_size * ACTION_COLUMN;
    const float path_x = rect.Min.x + padding + font_size * STATUS_COLUMN;
    const float path_max_x = rect.Max.x - action_width - padding * 2.0f;
    const bool is_skipped = planned.action == file_action::skip;
    const ImU32 name_color = is_skipped ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text);
    widgets::draw_path_text(draw_list,
                            ImVec2(path_x, ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f)),
                            path_max_x,
                            file.path,
                            name_color);
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::InvisibleButton("##row", ImVec2(path_max_x - rect.Min.x, rect.GetHeight()));
    if(ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted(file.path.c_str());
        ImGui::PushTextWrapPos(font_size * 28.0f);
        widgets::draw_muted_wrapped(describe_status(planned).c_str());
        const auto destination = describe_destination(planned);
        if(!destination.empty())
        {
            widgets::draw_muted_wrapped(destination.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Max.x - action_width - padding, rect.Min.y));
    if(planned.choices.size() > 1)
    {
        ImGui::SetNextItemWidth(action_width);
        if(ImGui::BeginCombo("##action", packages::get_file_action_name(planned.action)))
        {
            for(const auto choice : planned.choices)
            {
                if(ImGui::Selectable(packages::get_file_action_name(choice), choice == planned.action))
                {
                    planned.action = choice;
                    // Which scripts get written decides whether a type would be defined twice.
                    packages::update_script_clashes(plan_, project_);
                }
                ImGui::SetItemTooltipEx("%s", packages::describe_file_action(choice));
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltipEx("%s", packages::describe_file_action(planned.action));
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", planned.choices.empty() ? "Nothing to do" : packages::get_file_action_name(planned.action));
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x, rect.Max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

void package_import_view::draw_removal_row(std::size_t index, int row_index)
{
    auto& removal = plan_.removals[index];
    const float font_size = ImGui::GetFontSize();
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImRect rect(row_min, ImVec2(row_min.x + ImGui::GetContentRegionAvail().x, row_min.y + ImGui::GetFrameHeight()));
    ImGui::PushID(int(plan_.files.size() + index));
    const bool is_hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max) && ImGui::IsWindowHovered();
    widgets::draw_row_background(rect, row_index, is_hovered, false);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = font_size * ROW_PADDING_X;
    const char* status_name = removal.is_edited ? "Dropped, edited" : "Dropped";
    const ImVec2 pill_size = widgets::calc_pill_size(status_name);
    widgets::draw_pill(draw_list,
                       ImVec2(rect.Min.x + padding, rect.Min.y + (rect.GetHeight() - pill_size.y) * 0.5f),
                       status_name,
                       removal.is_edited ? widgets::COLOR_EDITED : widgets::COLOR_ERROR);
    const float action_width = font_size * ACTION_COLUMN;
    widgets::draw_path_text(draw_list,
                            ImVec2(rect.Min.x + padding + font_size * STATUS_COLUMN, ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f)),
                            rect.Max.x - action_width - padding * 2.0f,
                            packages::get_data_path(removal.key),
                            ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::InvisibleButton("##row", ImVec2(rect.GetWidth() - action_width - padding * 2.0f, rect.GetHeight()));
    ImGui::SetItemTooltipEx("%s",
                            removal.is_edited ? "The new version no longer has this file. You edited it, so it is kept by default."
                                              : "The new version no longer has this file.");
    ImGui::SetCursorScreenPos(ImVec2(rect.Max.x - action_width - padding, rect.Min.y));
    ImGui::SetNextItemWidth(action_width);
    if(ImGui::BeginCombo("##removal", removal.is_removed ? "To the trash" : "Keep"))
    {
        if(ImGui::Selectable("To the trash", removal.is_removed))
        {
            removal.is_removed = true;
            packages::update_script_clashes(plan_, project_);
        }
        ImGui::SetItemTooltipEx("%s", "The file goes to the project's trash, where it can be restored.");
        if(ImGui::Selectable("Keep", !removal.is_removed))
        {
            removal.is_removed = false;
            packages::update_script_clashes(plan_, project_);
        }
        ImGui::SetItemTooltipEx("%s", "The file stays in the project and is no longer part of the package.");
        ImGui::EndCombo();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x, rect.Max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

void package_import_view::draw_readme()
{
    if(readme_.empty())
    {
        return;
    }
    if(!panel_section::draw_header("##import_readme", ICON_MDI_TEXT_BOX_OUTLINE, "Read Me", false).is_open)
    {
        return;
    }
    if(ImGui::BeginChild("##import_readme_text", ImVec2(0.0f, widgets::to_pixels(README_MAX_HEIGHT)), ImGuiChildFlags_AutoResizeY))
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(readme_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

auto package_import_view::draw_footer(rtti::context& ctx) -> bool
{
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    ImGui::Separator();
    deploy_widgets::draw_gap(widgets::SECTION_GAP * 0.5f);
    bool is_kept_open = true;
    if(deploy_widgets::draw_button("Cancel", false, !is_applying_))
    {
        is_kept_open = false;
    }
    ImGui::SetItemTooltipEx("%s", is_applying_ ? "The import is writing the project and runs to its end." : "Close the review. Nothing is imported.");
    if(!open_error_.empty() || !has_plan_)
    {
        return is_kept_open;
    }
    const auto label = get_apply_label();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - deploy_widgets::calc_button_width(label.c_str())));
    const auto reason = get_blocking_reason();
    if(deploy_widgets::draw_button(label.c_str(), true, reason.empty()))
    {
        apply(ctx);
    }
    ImGui::SetItemTooltipEx("%s", reason.empty() ? "Write the files as listed. Replaced files go to the project's trash." : reason.c_str());
    return is_kept_open;
}

auto package_import_view::draw_result(rtti::context& ctx) -> bool
{
    if(widgets::begin_card("##import_result"))
    {
        const std::string title = fmt::format("{} {} is in the project", plan_.manifest.get_title(), plan_.manifest.version);
        widgets::draw_title(title.c_str());
        const std::string summary =
            fmt::format("{} file(s) written, {} kept as they were, and {} moved to the project's trash.",
                        result_.written_keys.size(),
                        result_.kept_count,
                        result_.removed_keys.size());
        deploy_widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE, widgets::COLOR_NEW, summary.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        widgets::draw_muted_wrapped("The editor imports the new files now. Its progress shows in the status bar.");
    }
    widgets::end_card();
    deploy_widgets::draw_gap(widgets::SECTION_GAP);
    bool is_kept_open = true;
    if(deploy_widgets::draw_button(ICON_MDI_FOLDER_OPEN_OUTLINE " Show Files", false, !plan_.manifest.root.empty()))
    {
        ctx.get_cached<editing_manager>().focus_path(fs::resolve_protocol(packages::get_data_key(plan_.manifest.root)));
    }
    ImGui::SetItemTooltipEx("%s", "Show the package's folder in the Content Browser.");
    ImGui::SameLine();
    const bool has_scene = !plan_.manifest.entry_scene.empty();
    if(has_scene)
    {
        if(deploy_widgets::draw_button(ICON_MDI_PLAY_BOX_OUTLINE " Open Demo Scene", false, true))
        {
            auto& am = ctx.get_cached<asset_manager>();
            editor_actions::open_scene_from_asset(ctx, am.get_asset<scene_prefab>(packages::get_data_key(plan_.manifest.entry_scene)));
        }
        ImGui::SetItemTooltipEx("%s", "Open the scene the package names as its demo.");
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - deploy_widgets::calc_button_width("Done")));
    if(deploy_widgets::draw_button("Done", true, true))
    {
        is_kept_open = false;
    }
    return is_kept_open;
}

void package_import_view::apply(rtti::context& ctx)
{
    // The project as it is now, read here on the main thread; the job plans again from it and imports only when the
    // plan is the one the review shows, so nothing changed since is written over.
    is_applying_ = true;
    apply_job_ = ctx.get_cached<threader>()
                     .pool
                     ->schedule("Importing Package",
                                [context = &ctx, archive = archive_, picked = plan_, project = package_actions::make_project_view(ctx)]()
                                {
                                    apply_outcome outcome;
                                    outcome.project = project;
                                    auto fresh = packages::plan_import(*archive, project);
                                    if(!packages::is_same_plan(picked, fresh))
                                    {
                                        packages::carry_actions(picked, fresh);
                                        packages::update_script_clashes(fresh, project);
                                        outcome.is_plan_changed = true;
                                        outcome.plan = std::move(fresh);
                                        return outcome;
                                    }
                                    outcome.result = package_actions::install(*context, *archive, picked, project);
                                    outcome.plan = picked;
                                    return outcome;
                                })
                     .share();
}

auto package_import_view::is_file_shown(std::size_t index) const -> bool
{
    const auto& planned = plan_.files[index];
    if(!shown_groups_[get_group(planned.status)])
    {
        return false;
    }
    return search_.PassFilter(plan_.manifest.files[planned.index].path.c_str());
}

auto package_import_view::get_apply_label() const -> std::string
{
    if(plan_.is_update() && plan_.installed->version != plan_.manifest.version)
    {
        return fmt::format(ICON_MDI_PACKAGE_UP " Update to {}", plan_.manifest.version);
    }
    const std::size_t written = std::size_t(std::count_if(plan_.files.begin(),
                                                          plan_.files.end(),
                                                          [](const packages::planned_file& planned)
                                                          {
                                                              return planned.action != file_action::skip &&
                                                                     planned.action != file_action::keep_mine &&
                                                                     planned.action != file_action::adopt;
                                                          }));
    return fmt::format(ICON_MDI_PACKAGE_DOWN " Import {} File{}", written, written == 1 ? "" : "s");
}

auto package_import_view::get_blocking_reason() const -> std::string
{
    if(is_applying_)
    {
        return "The package is being imported.";
    }
    if(!plan_.problems.empty())
    {
        return plan_.problems.front();
    }
    if(!plan_.clashes.empty())
    {
        return plan_.clashes.front();
    }
    if(!plan_.has_changes())
    {
        return "The project already has this package as it is. There is nothing to import.";
    }
    return {};
}

} // namespace unravel
