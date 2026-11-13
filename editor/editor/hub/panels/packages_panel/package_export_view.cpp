#include "package_export_view.h"
#include "packages_widgets.h"

#include "../deploy_panel/deploy_panel_widgets.h"
#include "../inspector_panel/inspectors/inspector.h"
#include "../panel_section.h"
#include "../panel_toolbar.h"

#include <editor/assets/package_actions.h>
#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_style.h>
#include <packages/package_archive.h>
#include <packages/package_version.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <logging/logging.h>

#include <algorithm>
#include <unordered_set>

namespace unravel
{
namespace
{
using packages::export_reason;

constexpr float CHECK_COLUMN = 1.9f;
constexpr float REASON_COLUMN = 9.0f;
constexpr float SIZE_COLUMN = 5.5f;
constexpr float DESCRIPTION_LINES = 3.0f;
constexpr std::size_t SCRIPT_ERRORS_SHOWN = 6;
/// How many times a failed check adds the scripts it misses and runs again.
constexpr int SCRIPT_RESOLVE_ROUNDS = 5;
constexpr const char* SCENE_TYPE = ".spfb";
/// The longest text a field of the form takes.
constexpr std::size_t FIELD_CAPACITY = 256;
constexpr std::size_t DESCRIPTION_CAPACITY = 2048;

auto get_reason_color(export_reason reason) -> ImU32
{
    switch(reason)
    {
        case export_reason::in_folder:
            return packages_widgets::COLOR_MUTED_PILL;
        case export_reason::referenced:
            return packages_widgets::COLOR_UPDATE;
        case export_reason::script:
            return packages_widgets::COLOR_NEW;
    }
    return packages_widgets::COLOR_MUTED_PILL;
}

/// A text field in the inspector's layout. @return True when the text changed.
auto draw_text_row(const char* label, const char* tooltip, std::string& text, const char* hint = "") -> bool
{
    property_layout layout(label, tooltip);
    ImGui::SetNextItemWidth(-FLT_MIN);
    auto buffer = ImGui::CreateInputTextBuffer<FIELD_CAPACITY>(text);
    if(!ImGui::InputTextWithHint(fmt::format("##{}", label).c_str(), hint, buffer.data(), buffer.size()))
    {
        return false;
    }
    text = buffer.data();
    return true;
}
} // namespace

void package_export_view::open(rtti::context& ctx, const std::string& folder_key)
{
    close();
    is_open_ = true;
    folder_key_ = folder_key;
    source_ = package_actions::make_export_source(ctx);
    auto source = source_;
    plan_job_ = ctx.get_cached<threader>()
                    .pool
                    ->schedule("Planning Package Export",
                               [source = std::move(source), folder_key]()
                               {
                                   return packages::plan_export(source, folder_key);
                               })
                    .share();
}

void package_export_view::close()
{
    is_open_ = false;
    source_ = {};
    added_script_count_ = 0;
    has_plan_ = false;
    plan_ = {};
    definition_ = {};
    plan_job_ = {};
    script_job_ = {};
    has_script_check_ = false;
    is_script_check_stale_ = false;
    script_check_ = {};
    name_error_.clear();
    export_error_.clear();
    is_exported_ = false;
    exported_file_.clear();
    search_.Clear();
}

void package_export_view::poll_jobs()
{
    if(!has_plan_ && plan_job_.valid() && plan_job_.is_ready())
    {
        try
        {
            plan_ = plan_job_.get();
            definition_ = plan_.definition;
            has_plan_ = true;
            // An export from the package's own folder goes on from the version it last wrote.
            if(plan_.has_definition)
            {
                if(const auto version = packages::parse_version(definition_.version))
                {
                    definition_.version = version->get_next_patch().to_string();
                }
            }
        }
        catch(const std::exception& e)
        {
            export_error_ = std::string("Planning the export failed: ") + e.what();
            has_plan_ = true;
        }
    }
    if(!has_script_check_ && !is_script_check_stale_ && script_job_.valid() && script_job_.is_ready())
    {
        auto check = script_job_.get();
        script_job_ = {};
        add_found_scripts(check);
        script_check_ = std::move(check);
        has_script_check_ = true;
    }
}

auto package_export_view::run_script_check(const packages::export_source& source, packages::export_plan plan) -> script_check
{
    script_check check;
    std::unordered_set<std::string> added_keys;
    for(int round = 0;; ++round)
    {
        check.builds = asset_compiler::check_script_files(plan.get_included_script_files(), check.errors);
        if(check.builds || round >= SCRIPT_RESOLVE_ROUNDS)
        {
            break;
        }
        // The scripts declaring what the build misses go in, and the build runs again with them.
        std::vector<std::string> messages;
        for(const auto& error : check.errors)
        {
            messages.push_back(error.msg);
        }
        const auto added = packages::add_scripts_declaring(source, packages::find_missing_type_names(messages), plan);
        if(added.empty())
        {
            break;
        }
        for(const auto& key : added)
        {
            added_keys.insert(fs::make_path_key(key));
        }
    }
    // By key: adding sorts the plan's entries by path.
    for(const auto& entry : plan.entries)
    {
        if(added_keys.count(fs::make_path_key(entry.key)) != 0)
        {
            check.added_entries.push_back(entry);
        }
    }
    for(const auto& type : plan.script_types)
    {
        if(added_keys.count(fs::make_path_key(type.key)) != 0)
        {
            check.added_types.push_back(type);
        }
    }
    return check;
}

void package_export_view::add_found_scripts(const script_check& check)
{
    for(const auto& entry : check.added_entries)
    {
        const bool is_planned = std::any_of(plan_.entries.begin(),
                                            plan_.entries.end(),
                                            [&entry](const packages::export_entry& planned)
                                            {
                                                return fs::make_path_key(planned.key) == fs::make_path_key(entry.key);
                                            });
        if(!is_planned)
        {
            plan_.entries.push_back(entry);
            ++added_script_count_;
        }
    }
    std::sort(plan_.entries.begin(),
              plan_.entries.end(),
              [](const packages::export_entry& lhs, const packages::export_entry& rhs)
              {
                  return lhs.path < rhs.path;
              });
    for(const auto& type : check.added_types)
    {
        const bool is_listed = std::any_of(plan_.script_types.begin(),
                                           plan_.script_types.end(),
                                           [&type](const packages::script_type_source& listed)
                                           {
                                               return listed.full_name == type.full_name && listed.key == type.key;
                                           });
        if(!is_listed)
        {
            plan_.script_types.push_back(type);
        }
    }
}

void package_export_view::start_script_check(rtti::context& ctx)
{
    has_script_check_ = false;
    script_check_ = {};
    if(plan_.get_included_script_files().empty())
    {
        has_script_check_ = true;
        script_check_.builds = true;
        return;
    }
    // Off the main thread: a failed build looks the missing types up in every script of the project.
    script_job_ = ctx.get_cached<threader>()
                      .pool
                      ->schedule("Checking Package Scripts",
                                 [source = source_, plan = plan_]()
                                 {
                                     return run_script_check(source, plan);
                                 })
                      .share();
}

auto package_export_view::draw(rtti::context& ctx) -> bool
{
    const bool had_plan = has_plan_;
    poll_jobs();
    if(!has_plan_)
    {
        draw_planning();
        return draw_footer(ctx);
    }
    if(!had_plan && export_error_.empty())
    {
        start_script_check(ctx);
    }
    if(is_exported_)
    {
        return draw_result();
    }
    draw_header();
    draw_form();
    draw_contents();
    draw_scripts();
    return draw_footer(ctx);
}

void package_export_view::draw_planning()
{
    if(packages_widgets::begin_card("##export_planning"))
    {
        packages_widgets::draw_title("Finding what the package needs");
        const std::string text = fmt::format("Following every reference from {}. This takes a moment in a large project.", folder_key_);
        packages_widgets::draw_muted_wrapped(text.c_str());
    }
    packages_widgets::end_card();
}

void package_export_view::draw_header()
{
    if(packages_widgets::begin_card("##export_header"))
    {
        const float tile_size = packages_widgets::to_pixels(packages_widgets::HEADER_TILE_SIZE);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        packages_widgets::draw_package_tile(ImGui::GetWindowDrawList(), origin, tile_size, definition_.name, ICON_MDI_PACKAGE_UP);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tile_size + packages_widgets::to_pixels(packages_widgets::HEADER_TILE_GAP), origin.y));
        ImGui::BeginGroup();
        const std::string title = fmt::format("Export {}", plan_.root);
        packages_widgets::draw_title(title.c_str());
        const std::string summary = fmt::format("{} file(s), {}, from {}",
                                                plan_.get_included_count(),
                                                format_bytes(plan_.get_included_size(), 1),
                                                plan_.folder_key);
        packages_widgets::draw_muted_wrapped(summary.c_str());
        packages_widgets::draw_muted_wrapped(plan_.has_definition ? "The folder's package.json gives the details below, and the export saves them back."
                                                                  : "The export saves the details below in the folder's package.json, so the next "
                                                                    "version keeps the same name.");
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, ImMax(ImGui::GetCursorScreenPos().y, origin.y + tile_size)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    packages_widgets::end_card();
}

void package_export_view::draw_form()
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    if(!panel_section::draw_header("##export_package", ICON_MDI_TAG_OUTLINE, "Package", true).is_open)
    {
        return;
    }
    deploy_widgets::push_field_colors();
    property_layout_group group("##export_form");
    if(draw_text_row("Name",
                     "The package's id. Projects tell its versions apart by it, so keep it once the package is shared.",
                     definition_.name,
                     "com.studio.ui-kit"))
    {
        name_error_.clear();
    }
    name_error_ = packages::check_package_name(definition_.name);
    draw_text_row("Display Name", "The name the Packages window shows.", definition_.display_name);
    draw_text_row("Version", "The package's version, as major.minor.patch. Projects update from a lower one to a higher one.", definition_.version, "1.0.0");
    draw_text_row("Author", "Who made the package.", definition_.author);
    {
        property_layout layout("Description", "What the package holds, shown before it is imported.");
        ImGui::InputTextWidget<DESCRIPTION_CAPACITY>("##description",
                                                     definition_.description,
                                                     true,
                                                     0,
                                                     ImVec2(-FLT_MIN,
                                                            ImGui::GetTextLineHeight() * DESCRIPTION_LINES + ImGui::GetStyle().FramePadding.y * 2.0f));
    }
    draw_text_row("License", "The license the package is shared under. A LICENSE file in the folder travels with it.", definition_.license, "MIT");
    draw_text_row("Minimum Engine",
                  "The oldest engine version the package works with. An older engine refuses it. Empty takes any.",
                  definition_.minimum_engine,
                  "1.2.0");
    {
        property_layout layout("Demo Scene", "A scene the import offers to open once the package is in.");
        const auto scenes = get_scene_paths();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(ImGui::BeginCombo("##entry_scene", definition_.entry_scene.empty() ? "None" : definition_.entry_scene.c_str()))
        {
            if(ImGui::Selectable("None", definition_.entry_scene.empty()))
            {
                definition_.entry_scene.clear();
            }
            for(const auto& scene : scenes)
            {
                if(ImGui::Selectable(scene.c_str(), scene == definition_.entry_scene))
                {
                    definition_.entry_scene = scene;
                }
            }
            ImGui::EndCombo();
        }
    }
    deploy_widgets::pop_field_colors();
    if(!name_error_.empty())
    {
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT, packages_widgets::COLOR_EDITED, name_error_.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
}

void package_export_view::draw_contents()
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const std::string title = fmt::format("Contents ({} of {})", plan_.get_included_count(), plan_.entries.size());
    if(!panel_section::draw_header("##export_contents", ICON_MDI_FILE_TREE, title.c_str(), true).is_open)
    {
        return;
    }
    const ImU32 muted = imgui_style::get_muted_text_color_u32();
    const std::size_t outside = std::size_t(std::count_if(plan_.entries.begin(),
                                                          plan_.entries.end(),
                                                          [](const packages::export_entry& entry)
                                                          {
                                                              return entry.is_included && entry.is_outside_root;
                                                          }));
    if(outside > 0)
    {
        const std::string note = fmt::format("{} file(s) outside the folder go in because the package uses them. They install at the "
                                             "same paths in other projects.",
                                             outside);
        deploy_widgets::draw_icon_line(ICON_MDI_INFORMATION_OUTLINE, packages_widgets::COLOR_UPDATE, note.c_str(), muted);
    }
    if(!plan_.engine_assets.empty())
    {
        const std::string note = fmt::format("{} engine asset(s) are used and stay out: every engine has them.", plan_.engine_assets.size());
        deploy_widgets::draw_icon_line(ICON_MDI_CUBE_OUTLINE, muted, note.c_str(), muted);
    }
    if(panel_toolbar::begin_strip("##export_search"))
    {
        const float font_size = ImGui::GetFontSize();
        panel_toolbar::search_field(search_,
                                    ICON_MDI_MAGNIFY " Search files...",
                                    "Show the files whose path holds the text.",
                                    font_size * packages_widgets::SEARCH_MIN_WIDTH,
                                    font_size * packages_widgets::SEARCH_WIDTH);
    }
    panel_toolbar::end_strip();
    std::vector<std::size_t> shown;
    for(std::size_t index = 0; index < plan_.entries.size(); ++index)
    {
        if(search_.PassFilter(plan_.entries[index].path.c_str()))
        {
            shown.push_back(index);
        }
    }
    const float row_height = ImGui::GetFrameHeight();
    const float wanted = row_height * float(ImMax<std::size_t>(shown.size(), 1)) + ImGui::GetStyle().WindowPadding.y;
    const float reserved = ImGui::GetFrameHeightWithSpacing() * 6.0f;
    const float height = ImClamp(wanted,
                                 packages_widgets::to_pixels(packages_widgets::LIST_MIN_HEIGHT),
                                 ImMax(packages_widgets::to_pixels(packages_widgets::LIST_MIN_HEIGHT), ImGui::GetContentRegionAvail().y - reserved));
    if(ImGui::BeginChild("##export_file_list", ImVec2(0.0f, height)))
    {
        if(shown.empty())
        {
            packages_widgets::draw_centered_hint("Nothing to show", plan_.entries.empty() ? "The folder holds no asset." : "No file passes the search.");
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGuiListClipper clipper;
        clipper.Begin(int(shown.size()), row_height);
        while(clipper.Step())
        {
            for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                draw_entry_row(shown[std::size_t(row)], row);
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void package_export_view::draw_entry_row(std::size_t index, int row_index)
{
    auto& entry = plan_.entries[index];
    const float font_size = ImGui::GetFontSize();
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImRect rect(row_min, ImVec2(row_min.x + ImGui::GetContentRegionAvail().x, row_min.y + ImGui::GetFrameHeight()));
    ImGui::PushID(int(index));
    const bool is_hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max) && ImGui::IsWindowHovered();
    packages_widgets::draw_row_background(rect, row_index, is_hovered, false);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = font_size * packages_widgets::ROW_PADDING_X;
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x + padding, rect.Min.y));
    if(ImGui::Checkbox("##included", &entry.is_included) && entry.type == ".cs")
    {
        is_script_check_stale_ = true;
    }
    ImGui::SetItemTooltipEx("%s", entry.is_included ? "The file goes into the package." : "The file stays out of the package.");
    const float size_width = font_size * SIZE_COLUMN;
    const float reason_width = font_size * REASON_COLUMN;
    const float path_x = rect.Min.x + padding + font_size * CHECK_COLUMN;
    const float path_max_x = rect.Max.x - size_width - reason_width - padding * 2.0f;
    const float text_y = ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f);
    const ImU32 name_color = entry.is_included ? ImGui::GetColorU32(ImGuiCol_Text) : imgui_style::get_muted_text_color_u32();
    packages_widgets::draw_path_text(draw_list, ImVec2(path_x, text_y), path_max_x, entry.path, name_color);
    const char* reason = packages::get_export_reason_name(entry.reason);
    const ImVec2 pill_size = packages_widgets::calc_pill_size(reason);
    packages_widgets::draw_pill(draw_list,
                                ImVec2(path_max_x + padding, rect.Min.y + (rect.GetHeight() - pill_size.y) * 0.5f),
                                reason,
                                get_reason_color(entry.reason));
    const auto size_text = format_bytes(entry.size, 1);
    const float size_text_width = ImGui::CalcTextSize(size_text.c_str()).x;
    draw_list->AddText(ImVec2(rect.Max.x - padding - size_text_width, text_y), imgui_style::get_muted_text_color_u32(), size_text.c_str());
    ImGui::SetCursorScreenPos(ImVec2(path_x, rect.Min.y));
    ImGui::InvisibleButton("##row", ImVec2(ImMax(1.0f, rect.Max.x - path_x), rect.GetHeight()));
    if(ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted(entry.key.c_str());
        ImGui::PushTextWrapPos(font_size * 28.0f);
        if(entry.reason == export_reason::in_folder)
        {
            packages_widgets::draw_muted_wrapped("It lies in the package's folder.");
        }
        else
        {
            const std::string used_by = fmt::format("{} uses it.", entry.used_by);
            packages_widgets::draw_muted_wrapped(used_by.c_str());
            packages_widgets::draw_muted_wrapped("It lies outside the folder and installs at the same path in other projects.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x, rect.Max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

void package_export_view::draw_scripts()
{
    const auto scripts = plan_.get_included_script_files();
    if(scripts.empty() && plan_.unresolved_script_types.empty())
    {
        return;
    }
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const std::string title = fmt::format("Scripts ({})", scripts.size());
    if(!panel_section::draw_header("##export_scripts", ICON_MDI_LANGUAGE_CSHARP, title.c_str(), true).is_open)
    {
        return;
    }
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 muted = imgui_style::get_muted_text_color_u32();
    if(added_script_count_ > 0)
    {
        const auto note = fmt::format("{} script(s) the package's scripts use went in, so they build on their own.", added_script_count_);
        deploy_widgets::draw_icon_line(ICON_MDI_PLUS_CIRCLE_OUTLINE, packages_widgets::COLOR_UPDATE, note.c_str(), text_color);
    }
    if(!has_script_check_)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_PROGRESS_CLOCK, muted, "Building the package's scripts on their own, as another project would.", muted);
    }
    else if(script_check_.builds)
    {
        deploy_widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE, packages_widgets::COLOR_NEW, "The package's scripts build on their own.", text_color);
    }
    else
    {
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON,
                                       packages_widgets::COLOR_ERROR,
                                       "The package's scripts do not build on their own. They use scripts the package leaves out. "
                                       "Add those to the folder, or the package breaks in other projects.",
                                       text_color);
        for(std::size_t index = 0; index < script_check_.errors.size() && index < SCRIPT_ERRORS_SHOWN; ++index)
        {
            const auto& error = script_check_.errors[index];
            const auto line = packages::format_compiler_message(error.file, error.line, error.msg);
            packages_widgets::draw_muted_wrapped(line.c_str());
        }
    }
    for(const auto& type : plan_.unresolved_script_types)
    {
        const auto note = fmt::format("The script type {} is used, but no script of the project defines it now.", type);
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT, packages_widgets::COLOR_EDITED, note.c_str(), text_color);
    }
    for(const auto& type : plan_.script_types)
    {
        if(type.full_name.find('.') == std::string::npos)
        {
            const auto note =
                fmt::format("{} has no namespace. A project with a type of the same name refuses the package.", type.full_name);
            deploy_widgets::draw_icon_line(ICON_MDI_INFORMATION_OUTLINE, packages_widgets::COLOR_EDITED, note.c_str(), text_color);
        }
    }
}

auto package_export_view::draw_footer(rtti::context& ctx) -> bool
{
    if(is_script_check_stale_ && has_plan_)
    {
        is_script_check_stale_ = false;
        start_script_check(ctx);
    }
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    ImGui::Separator();
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP * 0.5f);
    bool is_kept_open = true;
    if(deploy_widgets::draw_button("Cancel", false, true))
    {
        is_kept_open = false;
    }
    ImGui::SetItemTooltipEx("%s", "Close the export. Nothing is written.");
    if(!has_plan_)
    {
        return is_kept_open;
    }
    const std::string label = ICON_MDI_PACKAGE_UP " Export";
    const float button_width = deploy_widgets::calc_button_width(label.c_str());
    const auto destination = fmt::format("To {}/{}", packages::PACKAGES_FOLDER_NAME, get_output_file().filename().string());
    const float button_height = ImGui::GetItemRectSize().y;
    ImGui::SameLine();
    // Drawn centered on the buttons' line, with an item of their height under it, so the line stays level.
    const ImVec2 text_pos = ImGui::GetCursorScreenPos();
    const ImVec2 text_size = ImGui::CalcTextSize(destination.c_str());
    ImGui::GetWindowDrawList()->AddText(ImVec2(text_pos.x, text_pos.y + (button_height - text_size.y) * 0.5f),
                                        imgui_style::get_muted_text_color_u32(),
                                        destination.c_str());
    ImGui::Dummy(ImVec2(text_size.x, button_height));
    const std::string destination_tooltip = fmt::format("The package goes into the .packages folder at the project root ({}), "
                                                        "as a deploy goes into .deploy. The Packages window lists it there.",
                                                        package_actions::get_packages_folder().generic_string());
    ImGui::SetItemTooltipEx("%s", destination_tooltip.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - button_width));
    const auto reason = get_blocking_reason();
    if(deploy_widgets::draw_button(label.c_str(), true, reason.empty()))
    {
        export_package(ctx);
    }
    ImGui::SetItemTooltipEx("%s",
                            reason.empty() ? "Write the package file and save the details in the folder's package.json."
                                           : reason.c_str());
    if(!export_error_.empty())
    {
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, packages_widgets::COLOR_ERROR, export_error_.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    return is_kept_open;
}

auto package_export_view::draw_result() -> bool
{
    if(packages_widgets::begin_card("##export_result"))
    {
        const std::string title = fmt::format("{} {} is exported", exported_.get_title(), exported_.version);
        packages_widgets::draw_title(title.c_str());
        const std::string summary = fmt::format("{} file(s) in {}.", exported_.files.size(), exported_file_.filename().string());
        deploy_widgets::draw_icon_line(ICON_MDI_CHECK_CIRCLE, packages_widgets::COLOR_NEW, summary.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        packages_widgets::draw_muted_wrapped("It is in the project's .packages folder. Share the file, and any project imports it when "
                                             "it is dropped on the Content Browser or picked with Import.");
    }
    packages_widgets::end_card();
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    if(deploy_widgets::draw_button(ICON_MDI_FOLDER_OPEN_OUTLINE " Show in Folder", false, true))
    {
        fs::show_in_graphical_env(exported_file_);
    }
    ImGui::SetItemTooltipEx("%s", "Show the package file in the file browser.");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - deploy_widgets::calc_button_width("Done")));
    return !deploy_widgets::draw_button("Done", true, true);
}

void package_export_view::export_package(rtti::context& ctx)
{
    export_error_.clear();
    const auto output = get_output_file();
    if(!packages::export_package(plan_, definition_, output, export_error_, &exported_))
    {
        return;
    }
    std::string err;
    if(!package_actions::save_definition(ctx, plan_.root, definition_, err))
    {
        APPLOG_WARNING("The package is exported, but its details were not saved: {}", err);
    }
    exported_file_ = output;
    is_exported_ = true;
    APPLOG_INFO("Exported {} {} to {}.", exported_.get_title(), exported_.version, output.generic_string());
}

auto package_export_view::get_blocking_reason() const -> std::string
{
    if(!name_error_.empty())
    {
        return name_error_;
    }
    if(!packages::parse_version(definition_.version))
    {
        return "The version is written as major.minor.patch, such as 1.0.0.";
    }
    if(!definition_.minimum_engine.empty() && !version::parse(definition_.minimum_engine))
    {
        return "The minimum engine is a version such as 1.2.0, or empty.";
    }
    if(plan_.get_included_count() == 0)
    {
        return "The package would hold no file.";
    }
    if(!has_script_check_)
    {
        return "The scripts are still being built on their own.";
    }
    return {};
}

auto package_export_view::get_output_file() const -> fs::path
{
    return package_actions::get_packages_folder() / packages::get_package_file_name(definition_);
}

auto package_export_view::get_scene_paths() const -> std::vector<std::string>
{
    std::vector<std::string> scenes;
    for(const auto& entry : plan_.entries)
    {
        if(entry.is_included && entry.type == SCENE_TYPE)
        {
            scenes.push_back(entry.path);
        }
    }
    return scenes;
}

} // namespace unravel
