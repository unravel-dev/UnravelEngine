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

#include <algorithm>
#include <array>

namespace unravel
{
namespace
{
using packages::file_action;
using packages::file_status;

constexpr float STATUS_COLUMN = 7.5f;
constexpr float ACTION_COLUMN = 9.5f;

/// A counter of the review that filters its file list. A color of 0 draws the count in the text color.
struct import_file_filter
{
    const char* label;
    ImU32 color;
    const char* tooltip;
};

/// The status groups get_group gives, then the files the new version drops.
constexpr import_file_filter IMPORT_FILE_FILTERS[] = {
    {"New Files", packages_widgets::COLOR_NEW, "Show only the files the project does not have yet."},
    {"Updates", packages_widgets::COLOR_UPDATE, "Show only the files the package brings a new version of."},
    {"Edited by You", packages_widgets::COLOR_EDITED, "Show only the files you edited or deleted since an earlier version installed them."},
    {"Clashes", packages_widgets::COLOR_CLASH, "Show only the files whose id or place another file of the project has."},
    {"Other", 0, "Show only the files that need nothing: unchanged, already here, or not readable."},
    {"Dropped", packages_widgets::COLOR_ERROR, "Show only the files the new version no longer has."},
};
constexpr std::size_t IMPORT_FILE_FILTER_COUNT = sizeof(IMPORT_FILE_FILTERS) / sizeof(IMPORT_FILE_FILTERS[0]);
constexpr std::size_t DROPPED_FILES_FILTER = IMPORT_FILE_FILTER_COUNT - 1;
constexpr const char* TRASH_CHOICE_NAME = "To the trash";
constexpr const char* TRASH_CHOICE_DESCRIPTION = "The file goes to the project's trash, where it can be restored.";
constexpr const char* KEEP_CHOICE_NAME = "Keep";
constexpr const char* KEEP_CHOICE_DESCRIPTION = "The file stays in the project and is no longer part of the package.";

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
            return packages_widgets::COLOR_NEW;
        case file_status::updated:
            return packages_widgets::COLOR_UPDATE;
        case file_status::edited:
        case file_status::deleted:
            return packages_widgets::COLOR_EDITED;
        case file_status::uid_clash:
        case file_status::path_clash:
            return packages_widgets::COLOR_CLASH;
        case file_status::incompatible:
            return packages_widgets::COLOR_ERROR;
        default:
            return packages_widgets::COLOR_MUTED_PILL;
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
    if(apply_job_.valid())
    {
        apply_job_.wait();
    }
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
    active_filter_.reset();
    files_state_.clear();
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
    draw_filters();
    draw_files();
    draw_readme();
    return draw_footer(ctx);
}

void package_import_view::draw_progress(const char* title, const std::string& text)
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    if(packages_widgets::begin_card("##import_progress"))
    {
        packages_widgets::draw_title(title);
        deploy_widgets::draw_icon_line(ICON_MDI_PROGRESS_CLOCK, imgui_style::get_muted_text_color_u32(), text.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    packages_widgets::end_card();
}

void package_import_view::draw_open_error()
{
    if(packages_widgets::begin_card("##import_error"))
    {
        packages_widgets::draw_title("This package cannot be opened");
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, packages_widgets::COLOR_ERROR, open_error_.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        packages_widgets::draw_muted_wrapped(package_file_.generic_string().c_str());
    }
    packages_widgets::end_card();
}

void package_import_view::draw_header()
{
    const auto& manifest = plan_.manifest;
    if(packages_widgets::begin_card("##import_header"))
    {
        const float tile_size = packages_widgets::to_pixels(packages_widgets::HEADER_TILE_SIZE);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        packages_widgets::draw_package_tile(ImGui::GetWindowDrawList(), origin, tile_size, manifest.name);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tile_size + packages_widgets::to_pixels(packages_widgets::HEADER_TILE_GAP), origin.y));
        ImGui::BeginGroup();
        const std::string title = plan_.is_update() && plan_.installed->version != manifest.version
                                      ? fmt::format("Update {} {} to {}", manifest.get_title(), plan_.installed->version, manifest.version)
                                      : fmt::format("Import {} {}", manifest.get_title(), manifest.version);
        packages_widgets::draw_title(title.c_str());
        std::string byline = manifest.name;
        if(!manifest.author.empty())
        {
            byline += "   by " + manifest.author;
        }
        byline += "   " + get_package_size_text(*archive_) + "   made with engine " + manifest.exported_with;
        packages_widgets::draw_muted_wrapped(byline.c_str());
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
    packages_widgets::end_card();
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
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    for(const auto* sentences : {&plan_.problems, &plan_.clashes})
    {
        for(const auto& problem : *sentences)
        {
            deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, packages_widgets::COLOR_ERROR, problem.c_str(), text_color);
        }
    }
    for(const auto& warning : plan_.warnings)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT, packages_widgets::COLOR_EDITED, warning.c_str(), text_color);
    }
    if(has_scripts)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_LANGUAGE_CSHARP,
                                       imgui_style::get_muted_text_color_u32(),
                                       "It holds C# scripts. Scripts run in the editor, so import packages from people you trust.",
                                       imgui_style::get_muted_text_color_u32());
    }
}

void package_import_view::draw_filters()
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    std::array<std::size_t, IMPORT_FILE_FILTER_COUNT> counts{};
    for(const auto& planned : plan_.files)
    {
        ++counts[get_group(planned.status)];
    }
    counts[DROPPED_FILES_FILTER] = plan_.removals.size();
    const std::size_t trashed = std::size_t(std::count_if(plan_.removals.begin(),
                                                          plan_.removals.end(),
                                                          [](const packages::planned_removal& removal)
                                                          {
                                                              return removal.is_removed;
                                                          }));
    const std::string dropped_tooltip =
        fmt::format("{} {} of them go to the project's trash.", IMPORT_FILE_FILTERS[DROPPED_FILES_FILTER].tooltip, trashed);
    const float gap = ImGui::GetFontSize() * 0.5f;
    const float width = packages_widgets::calc_tile_width(int(IMPORT_FILE_FILTER_COUNT));
    for(std::size_t filter = 0; filter < IMPORT_FILE_FILTER_COUNT; ++filter)
    {
        if(filter > 0)
        {
            ImGui::SameLine(0.0f, gap);
        }
        const auto& entry = IMPORT_FILE_FILTERS[filter];
        packages_widgets::filter_tile tile;
        tile.label = entry.label;
        tile.count = counts[filter];
        tile.color = entry.color != 0 ? entry.color : ImGui::GetColorU32(ImGuiCol_Text);
        tile.is_active = active_filter_ == filter;
        if(tile.is_active)
        {
            tile.tooltip = "Show every file again.";
        }
        else if(tile.count > 0)
        {
            tile.tooltip = filter == DROPPED_FILES_FILTER ? dropped_tooltip.c_str() : entry.tooltip;
        }
        const std::string id = fmt::format("##filter_{}", filter);
        if(packages_widgets::draw_filter_tile(id.c_str(), tile, width))
        {
            active_filter_ = tile.is_active ? hpp::optional<std::size_t>{} : hpp::optional<std::size_t>{filter};
        }
    }
}

void package_import_view::draw_files()
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const std::string title = fmt::format("Files ({})", plan_.files.size());
    if(!panel_section::draw_header("##import_files", ICON_MDI_FILE_TREE, title.c_str(), true).is_open)
    {
        return;
    }
    draw_file_toolbar();
    // The list's rows: the shown planned files, then the shown files the new version drops.
    std::vector<std::size_t> shown_files;
    std::vector<std::size_t> shown_removals;
    std::vector<packages_widgets::file_list_row> rows;
    for(std::size_t index = 0; index < plan_.files.size(); ++index)
    {
        if(is_file_shown(index))
        {
            const auto& planned = plan_.files[index];
            const auto& file = plan_.manifest.files[planned.index];
            shown_files.push_back(index);
            rows.push_back({file.path,
                            file.size,
                            packages::get_file_status_name(planned.status),
                            get_status_color(planned.status),
                            planned.action == file_action::skip});
        }
    }
    for(std::size_t index = 0; index < plan_.removals.size(); ++index)
    {
        if(is_removal_shown(index))
        {
            const auto& removal = plan_.removals[index];
            shown_removals.push_back(index);
            rows.push_back({packages::get_data_path(removal.key),
                            {},
                            removal.is_edited ? "Dropped, edited" : "Dropped",
                            removal.is_edited ? packages_widgets::COLOR_EDITED : packages_widgets::COLOR_ERROR,
                            false});
        }
    }
    const std::size_t file_count = shown_files.size();
    packages_widgets::file_list_parts parts;
    parts.label_width = STATUS_COLUMN;
    parts.trailing_width = ACTION_COLUMN;
    parts.draw_trailing = [&](std::size_t row)
    {
        if(row < file_count)
        {
            draw_file_choice(shown_files[row]);
            return;
        }
        draw_removal_choice(shown_removals[row - file_count]);
    };
    parts.draw_tooltip = [&](std::size_t row)
    {
        if(row < file_count)
        {
            draw_file_tooltip(shown_files[row]);
            return;
        }
        draw_removal_tooltip(shown_removals[row - file_count]);
    };
    const float min_height = packages_widgets::to_pixels(packages_widgets::LIST_MIN_HEIGHT);
    const float reserved = ImGui::GetFrameHeightWithSpacing() * 3.0f;
    packages_widgets::draw_file_list("##import_file_list",
                                     rows,
                                     files_state_,
                                     parts,
                                     ImMax(min_height, ImGui::GetContentRegionAvail().y - reserved),
                                     "No file passes the search and the filter.");
}

void package_import_view::draw_file_toolbar()
{
    if(panel_toolbar::begin_strip("##import_file_toolbar"))
    {
        const float font_size = ImGui::GetFontSize();
        panel_toolbar::search_field(search_,
                                    ICON_MDI_MAGNIFY " Search files...",
                                    "Show the files whose path holds the text.",
                                    font_size * packages_widgets::SEARCH_MIN_WIDTH,
                                    font_size * packages_widgets::SEARCH_WIDTH);
        panel_toolbar::align_right();
        packages_widgets::draw_collapse_button("##import_collapse", files_state_);
    }
    panel_toolbar::end_strip();
}

void package_import_view::draw_file_choice(std::size_t index)
{
    auto& planned = plan_.files[index];
    if(planned.choices.size() <= 1)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", planned.choices.empty() ? "Nothing to do" : packages::get_file_action_name(planned.action));
        if(!planned.choices.empty())
        {
            ImGui::SetItemTooltipEx("%s", packages::describe_file_action(planned.action));
        }
        return;
    }
    if(packages_widgets::begin_row_choice("##action", packages::get_file_action_name(planned.action), ImGui::GetFontSize() * ACTION_COLUMN))
    {
        for(const auto choice : planned.choices)
        {
            if(packages_widgets::draw_choice(packages::get_file_action_name(choice), packages::describe_file_action(choice), choice == planned.action))
            {
                planned.action = choice;
                // Which scripts get written decides whether a type would be defined twice.
                packages::update_script_clashes(plan_, project_);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("%s", packages::describe_file_action(planned.action));
}

void package_import_view::draw_removal_choice(std::size_t index)
{
    auto& removal = plan_.removals[index];
    if(packages_widgets::begin_row_choice("##removal", removal.is_removed ? TRASH_CHOICE_NAME : KEEP_CHOICE_NAME, ImGui::GetFontSize() * ACTION_COLUMN))
    {
        if(packages_widgets::draw_choice(TRASH_CHOICE_NAME, TRASH_CHOICE_DESCRIPTION, removal.is_removed))
        {
            removal.is_removed = true;
            packages::update_script_clashes(plan_, project_);
        }
        if(packages_widgets::draw_choice(KEEP_CHOICE_NAME, KEEP_CHOICE_DESCRIPTION, !removal.is_removed))
        {
            removal.is_removed = false;
            packages::update_script_clashes(plan_, project_);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("%s", removal.is_removed ? TRASH_CHOICE_DESCRIPTION : KEEP_CHOICE_DESCRIPTION);
}

void package_import_view::draw_file_tooltip(std::size_t index) const
{
    const auto& planned = plan_.files[index];
    ImGui::TextUnformatted(plan_.manifest.files[planned.index].path.c_str());
    packages_widgets::draw_muted_wrapped(describe_status(planned).c_str());
    const auto destination = describe_destination(planned);
    if(!destination.empty())
    {
        packages_widgets::draw_muted_wrapped(destination.c_str());
    }
}

void package_import_view::draw_removal_tooltip(std::size_t index) const
{
    const auto& removal = plan_.removals[index];
    ImGui::TextUnformatted(packages::get_data_path(removal.key).c_str());
    packages_widgets::draw_muted_wrapped(removal.is_edited
                                             ? "The new version no longer has this file. You edited it, so it is kept by default."
                                             : "The new version no longer has this file.");
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
    if(ImGui::BeginChild("##import_readme_text", ImVec2(0.0f, packages_widgets::to_pixels(packages_widgets::README_MAX_HEIGHT)), ImGuiChildFlags_AutoResizeY))
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(readme_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

auto package_import_view::draw_footer(rtti::context& ctx) -> bool
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    ImGui::Separator();
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP * 0.5f);
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
    if(packages_widgets::begin_card("##import_result"))
    {
        const std::string title = fmt::format("{} {} is in the project", plan_.manifest.get_title(), plan_.manifest.version);
        packages_widgets::draw_title(title.c_str());
        const std::string summary =
            fmt::format("{} file(s) written, {} kept as they were, and {} moved to the project's trash.",
                        result_.written_keys.size(),
                        result_.kept_count,
                        result_.removed_keys.size());
        deploy_widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE, packages_widgets::COLOR_NEW, summary.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        packages_widgets::draw_muted_wrapped("The editor imports the new files now. Its progress shows in the status bar.");
    }
    packages_widgets::end_card();
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
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
    if(active_filter_ && *active_filter_ != get_group(planned.status))
    {
        return false;
    }
    return search_.PassFilter(plan_.manifest.files[planned.index].path.c_str());
}

auto package_import_view::is_removal_shown(std::size_t index) const -> bool
{
    if(active_filter_ && *active_filter_ != DROPPED_FILES_FILTER)
    {
        return false;
    }
    return search_.PassFilter(packages::get_data_path(plan_.removals[index].key).c_str());
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
