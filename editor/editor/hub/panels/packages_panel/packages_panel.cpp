#include "packages_panel.h"
#include "packages_widgets.h"

#include "../deploy_panel/deploy_panel_widgets.h"
#include "../inspector_panel/inspectors/inspector.h"
#include "../panel_section.h"
#include "../panel_toolbar.h"

#include <editor/assets/package_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/events.h>
#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/system/project_manager.h>
#include <packages/package_archive.h>
#include <packages/package_version.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <set>

namespace unravel
{
namespace
{
constexpr float LIST_WIDTH = 19.0f;
constexpr float LIST_MIN_WIDTH = 12.0f;
constexpr float LIST_ROW_HEIGHT = 2.9f;
constexpr float LIST_TILE_SIZE = 2.0f;
constexpr float LIST_PADDING_X = 0.55f;
constexpr float LIST_TILE_GAP = 0.6f;
constexpr float DETAILS_TILE_SIZE = 3.4f;
constexpr float DETAILS_TILE_GAP = 0.85f;
constexpr float TOOLBAR_SEARCH_WIDTH = 13.0f;
constexpr float FILE_STATUS_COLUMN = 5.5f;
constexpr float FILES_MAX_HEIGHT = 16.0f;
constexpr int DETAILS_COUNTER_COUNT = 4;

/// The top folders of the project's data folder: what the toolbar's Export menu offers.
auto list_top_folders() -> std::vector<std::string>
{
    std::vector<std::string> folders;
    fs::error_code ec;
    const auto data = fs::resolve_protocol("app:/data");
    for(fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            folders.push_back(it->path().filename().string());
        }
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

auto count_versions(const std::vector<packages::library_entry>& library, const std::string& name) -> std::size_t
{
    return std::size_t(std::count_if(library.begin(),
                                     library.end(),
                                     [&name](const packages::library_entry& entry)
                                     {
                                         return entry.error.empty() && entry.manifest.name == name;
                                     }));
}

auto is_newer(const std::string& candidate, const std::string& current) -> bool
{
    const auto left = packages::parse_version(candidate);
    const auto right = packages::parse_version(current);
    return left && right && *left > *right;
}

void draw_value_row(const char* label, const char* tooltip, const std::string& value)
{
    property_layout layout(label, tooltip);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value.empty() ? "-" : value.c_str());
}
} // namespace

packages_panel::packages_panel(imgui_panels* parent) : parent_(parent)
{
}

void packages_panel::init(rtti::context& ctx)
{
    auto& ui_ev = ctx.get_cached<ui_events>();
    ui_ev.on_close_project.connect(sentinel_, 100, this, &packages_panel::on_project_closed);
}

void packages_panel::on_project_closed(rtti::context& /*ctx*/)
{
    close_reviews();
    registry_ = {};
    registry_error_.clear();
    library_.clear();
    installed_items_.clear();
    library_items_.clear();
    selected_name_.clear();
    selected_state_ = {};
    selected_readme_.clear();
    is_selection_pending_ = false;
    is_selected_authored_ = false;
    search_.Clear();
    installed_files_state_.clear();
    library_files_state_.clear();
    message_.clear();
    is_message_error_ = false;
    is_refresh_pending_ = true;
}

void packages_panel::close_reviews()
{
    import_view_.close();
    export_view_.close();
    queued_imports_.clear();
}

void packages_panel::show(bool s)
{
    show_request_ = s;
}

void packages_panel::open_import(rtti::context& ctx, const fs::path& package_file)
{
    show(true);
    if(import_view_.is_open())
    {
        queued_imports_.push_back(package_file);
        return;
    }
    export_view_.close();
    import_view_.open(ctx, package_file);
}

void packages_panel::open_imports(rtti::context& ctx, const std::vector<fs::path>& package_files)
{
    for(const auto& file : package_files)
    {
        open_import(ctx, file);
    }
}

void packages_panel::open_export(rtti::context& ctx, const std::string& folder_key)
{
    show(true);
    if(import_view_.is_busy())
    {
        APPLOG_WARNING("A package is being imported. Export {} once it is done.", folder_key);
        return;
    }
    import_view_.close();
    export_view_.open(ctx, folder_key);
}

void packages_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        is_refresh_pending_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * ImVec2(0.6f, 0.7f), ImGuiCond_Once);
        ImGui::SetWindowFocus(name);
    }
    if(!show_)
    {
        return;
    }
    // No close button while an import writes the project: it cannot stop halfway.
    if(ImGui::Begin(name, import_view_.is_busy() ? nullptr : &show_))
    {
        draw_ui(ctx);
    }
    ImGui::End();
    if(!show_)
    {
        close_reviews();
    }
}

void packages_panel::draw_ui(rtti::context& ctx)
{
    if(!ctx.get_cached<project_manager>().has_open_project())
    {
        packages_widgets::draw_centered_hint("No project is open", "Open a project to manage its packages.");
        return;
    }
    if(is_refresh_pending_)
    {
        refresh(ctx);
    }
    if(is_selection_pending_)
    {
        is_selection_pending_ = false;
        select(ctx, selected_name_);
    }
    if(import_view_.is_open())
    {
        const auto title = queued_imports_.empty() ? std::string("Import Package")
                                                   : fmt::format("Import Package ({} more waiting)", queued_imports_.size());
        draw_review_toolbar(title.c_str());
        if(!import_view_.draw(ctx))
        {
            const auto imported = import_view_.get_package_name();
            import_view_.close();
            refresh(ctx);
            select(ctx, imported);
            if(!queued_imports_.empty())
            {
                const auto next = queued_imports_.front();
                queued_imports_.pop_front();
                open_import(ctx, next);
            }
        }
        return;
    }
    if(export_view_.is_open())
    {
        draw_review_toolbar("Export Package");
        if(!export_view_.draw(ctx))
        {
            export_view_.close();
            refresh(ctx);
        }
        return;
    }
    draw_toolbar(ctx);
    draw_browse(ctx);
}

void packages_panel::draw_toolbar(rtti::context& ctx)
{
    if(panel_toolbar::begin_strip("##packages_toolbar"))
    {
        if(panel_toolbar::button("##import", ICON_MDI_PACKAGE_DOWN " Import...", "Pick a package file and review its import.", true))
        {
            pick_package_file(ctx);
        }
        draw_export_menu(ctx);
        panel_toolbar::separator();
        const float font_size = ImGui::GetFontSize();
        panel_toolbar::search_field(search_,
                                    ICON_MDI_MAGNIFY " Search packages...",
                                    "Show the packages whose name, title or author holds the text.",
                                    font_size * packages_widgets::SEARCH_MIN_WIDTH,
                                    font_size * TOOLBAR_SEARCH_WIDTH);
        panel_toolbar::align_right();
        if(panel_toolbar::button("##packages_folder",
                                 ICON_MDI_FOLDER_OPEN_OUTLINE " .packages",
                                 "Show the project's .packages folder in the file browser. Exports go there, and package "
                                 "files put there show up here."))
        {
            show_packages_folder();
        }
        if(panel_toolbar::button("##refresh", ICON_MDI_REFRESH, "Read the .packages folder and the project's packages again."))
        {
            refresh(ctx);
        }
    }
    panel_toolbar::end_strip();
}

void packages_panel::draw_review_toolbar(const char* title)
{
    if(panel_toolbar::begin_strip("##packages_review_toolbar"))
    {
        panel_toolbar::label(ICON_MDI_PACKAGE_VARIANT " Packages");
        panel_toolbar::path_separator();
        panel_toolbar::label(title);
    }
    panel_toolbar::end_strip();
}

void packages_panel::draw_export_menu(rtti::context& ctx)
{
    if(!panel_toolbar::begin_dropdown("##export", ICON_MDI_PACKAGE_UP " Export", "Export a folder of the project as a package."))
    {
        return;
    }
    const auto folders = list_top_folders();
    ImGui::TextDisabled("%s", "Export a folder of the project");
    ImGui::Separator();
    for(const auto& folder : folders)
    {
        if(ImGui::MenuItemIcon(ICON_MDI_FOLDER, folder.c_str()))
        {
            open_export(ctx, packages::get_data_key(folder));
        }
    }
    if(folders.empty())
    {
        ImGui::TextDisabled("%s", "The project's data folder holds no folder.");
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", "Any folder: right-click it in the Content Browser.");
    panel_toolbar::end_dropdown();
}

void packages_panel::draw_browse(rtti::context& ctx)
{
    const float font_size = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float list_width = ImClamp(font_size * LIST_WIDTH, font_size * LIST_MIN_WIDTH, avail * 0.45f);
    if(!registry_error_.empty())
    {
        const auto text = fmt::format("The project's record of its packages does not read, so imports and removals wait "
                                      "until it is fixed or moved away. {}",
                                      registry_error_);
        deploy_widgets::draw_icon_line(ICON_MDI_ALERT_OCTAGON, packages_widgets::COLOR_ERROR, text.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    }
    if(installed_items_.empty() && library_items_.empty())
    {
        packages_widgets::draw_centered_hint("No packages yet",
                                             "Export a folder with Export, or drop a .unravelpkg file on the Content Browser.");
        return;
    }
    if(ImGui::BeginChild("##packages_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_None))
    {
        draw_list();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if(ImGui::BeginChild("##packages_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
    {
        draw_details(ctx);
    }
    ImGui::EndChild();
}

void packages_panel::draw_list()
{
    draw_list_section("##installed", ICON_MDI_PACKAGE_VARIANT, "In This Project", installed_items_);
    draw_list_section("##library", ICON_MDI_FOLDER_OUTLINE, "In .packages", library_items_);
}

void packages_panel::draw_list_section(const char* id, const char* icon, const char* title, const std::vector<list_item>& items)
{
    std::vector<const list_item*> shown;
    for(const auto& item : items)
    {
        if(is_item_shown(item))
        {
            shown.push_back(&item);
        }
    }
    const std::string header = fmt::format("{} ({})", title, shown.size());
    if(!panel_section::draw_header(id, icon, header.c_str(), true).is_open)
    {
        return;
    }
    if(shown.empty())
    {
        ImGui::Indent(panel_section::get_row_indent());
        ImGui::TextDisabled("%s", items.empty() ? "None." : "No package passes the search.");
        ImGui::Unindent(panel_section::get_row_indent());
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for(std::size_t index = 0; index < shown.size(); ++index)
    {
        draw_list_row(*shown[index], int(index));
    }
    ImGui::PopStyleVar();
}

void packages_panel::draw_list_row(const list_item& item, int row_index)
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, font_size * LIST_ROW_HEIGHT);
    ImGui::PushID(item.name.c_str());
    const bool is_pressed = ImGui::InvisibleButton("##row", size);
    const bool is_selected = item.name == selected_name_;
    const ImRect rect(min, ImVec2(min.x + size.x, min.y + size.y));
    packages_widgets::draw_row_background(rect, row_index, ImGui::IsItemHovered(), is_selected);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = font_size * LIST_PADDING_X;
    const float tile = font_size * LIST_TILE_SIZE;
    packages_widgets::draw_package_tile(draw_list, ImVec2(min.x + padding, min.y + (size.y - tile) * 0.5f), tile, item.name);
    const float text_x = min.x + padding + tile + font_size * LIST_TILE_GAP;
    const auto& title = item.installed != nullptr ? item.installed->get_title() : item.newest->manifest.get_title();
    const auto& version = item.installed != nullptr ? item.installed->version : item.newest->manifest.version;
    // The state pill sits at the right end; the text gives way to it.
    const char* state = nullptr;
    ImU32 state_color = packages_widgets::COLOR_MUTED_PILL;
    if(item.installed != nullptr && item.newest != nullptr && is_newer(item.newest->manifest.version, item.installed->version))
    {
        state = "Update";
        state_color = packages_widgets::COLOR_UPDATE;
    }
    else if(item.installed == nullptr)
    {
        state = "Not installed";
    }
    float text_max_x = rect.Max.x - padding;
    if(state != nullptr)
    {
        const ImVec2 pill = packages_widgets::calc_pill_size(state);
        text_max_x -= pill.x + padding;
        packages_widgets::draw_pill(draw_list, ImVec2(rect.Max.x - padding - pill.x, min.y + (size.y - pill.y) * 0.5f), state, state_color);
    }
    const float line_gap = font_size * 0.15f;
    const float top = min.y + (size.y - font_size * 2.0f - line_gap) * 0.5f;
    const ImVec4 clip(text_x, min.y, text_max_x, rect.Max.y);
    draw_list->AddText(ImGui::GetFont(ImGui::Font::Bold), font_size, ImVec2(text_x, top), ImGui::GetColorU32(ImGuiCol_Text), title.c_str(), nullptr, 0.0f, &clip);
    const std::string detail = fmt::format("{}   {}", version, item.name);
    draw_list->AddText(ImGui::GetFont(), font_size, ImVec2(text_x, top + font_size + line_gap), imgui_style::get_muted_text_color_u32(), detail.c_str(), nullptr, 0.0f, &clip);
    if(is_pressed && item.name != selected_name_)
    {
        selected_name_ = item.name;
        is_selection_pending_ = true;
    }
    ImGui::PopID();
}

void packages_panel::draw_details(rtti::context& ctx)
{
    const auto* item = find_item(selected_name_);
    if(item == nullptr)
    {
        packages_widgets::draw_centered_hint("No package selected", "Pick a package in the list to see what it holds.");
        return;
    }
    if(item->installed != nullptr)
    {
        draw_installed_details(ctx, *item);
    }
    else
    {
        draw_library_details(ctx, *item);
    }
    draw_message();
}

void packages_panel::draw_details_header(const std::string& name,
                                         const std::string& title,
                                         const std::string& byline,
                                         const std::string& description,
                                         const char* state,
                                         ImU32 state_color)
{
    if(packages_widgets::begin_card("##details_header"))
    {
        const float tile_size = packages_widgets::to_pixels(DETAILS_TILE_SIZE);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        packages_widgets::draw_package_tile(ImGui::GetWindowDrawList(), origin, tile_size, name);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tile_size + packages_widgets::to_pixels(DETAILS_TILE_GAP), origin.y));
        ImGui::BeginGroup();
        packages_widgets::draw_title(title.c_str());
        if(state != nullptr)
        {
            ImGui::SameLine();
            packages_widgets::pill_item(state, state_color);
        }
        packages_widgets::draw_muted_wrapped(byline.c_str());
        if(!description.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, ImMax(ImGui::GetCursorScreenPos().y, origin.y + tile_size)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    packages_widgets::end_card();
}

void packages_panel::draw_installed_details(rtti::context& ctx, const list_item& item)
{
    const auto& package = *item.installed;
    std::string byline = package.name + "   version " + package.version;
    if(!package.author.empty())
    {
        byline += "   by " + package.author;
    }
    draw_details_header(package.name, package.get_title(), byline, package.description, "Installed", packages_widgets::COLOR_NEW);
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const bool has_update = item.newest != nullptr && is_newer(item.newest->manifest.version, package.version);
    if(has_update)
    {
        const auto label = fmt::format(ICON_MDI_PACKAGE_UP " Update to {}", item.newest->manifest.version);
        if(deploy_widgets::draw_button(label.c_str(), true, true))
        {
            open_import(ctx, item.newest->file);
        }
        ImGui::SetItemTooltipEx("%s", "Review the update from the .packages folder before anything changes.");
        ImGui::SameLine();
    }
    if(deploy_widgets::draw_button(ICON_MDI_FOLDER_OPEN_OUTLINE " Show Files", false, !package.root.empty()))
    {
        ctx.get_cached<editing_manager>().focus_path(fs::resolve_protocol(packages::get_data_key(package.root)));
    }
    ImGui::SetItemTooltipEx("%s", "Show the package's folder in the Content Browser.");
    if(is_selected_authored_)
    {
        ImGui::SameLine();
        if(deploy_widgets::draw_button(ICON_MDI_PACKAGE_UP " Export New Version", false, true))
        {
            open_export(ctx, packages::get_data_key(package.root));
        }
        ImGui::SetItemTooltipEx("%s", "The package's source folder is in this project. Export it again as the next version.");
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImMax(0.0f, ImGui::GetContentRegionAvail().x - deploy_widgets::calc_button_width(ICON_MDI_DELETE_OUTLINE " Remove...")));
    if(deploy_widgets::draw_button(ICON_MDI_DELETE_OUTLINE " Remove...", false, true))
    {
        confirm_remove(ctx);
    }
    ImGui::SetItemTooltipEx("%s", "Remove the package from the project. Its unchanged files go to the trash, and edited ones stay.");
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const std::size_t edited = std::size_t(std::count_if(selected_state_.files.begin(),
                                                         selected_state_.files.end(),
                                                         [](const packages::planned_removal& file)
                                                         {
                                                             return file.is_edited;
                                                         }));
    const std::size_t missing = package.files.size() - ImMin(package.files.size(), selected_state_.files.size());
    const float gap = ImGui::GetFontSize() * 0.5f;
    const float width = packages_widgets::calc_tile_width(DETAILS_COUNTER_COUNT);
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    packages_widgets::draw_tile("Files", std::to_string(package.files.size()), text_color, width);
    ImGui::SameLine(0.0f, gap);
    packages_widgets::draw_tile("Edited by You", std::to_string(edited), edited > 0 ? packages_widgets::COLOR_EDITED : text_color, width);
    ImGui::SameLine(0.0f, gap);
    packages_widgets::draw_tile("Deleted by You", std::to_string(missing), missing > 0 ? packages_widgets::COLOR_EDITED : text_color, width);
    ImGui::SameLine(0.0f, gap);
    packages_widgets::draw_tile("In .packages",
                                item.newest != nullptr ? item.newest->manifest.version : std::string("-"),
                                has_update ? packages_widgets::COLOR_UPDATE : text_color,
                                width);
    draw_installed_files(package);
    if(panel_section::draw_header("##installed_details", ICON_MDI_INFORMATION_OUTLINE, "Details", false).is_open)
    {
        property_layout_group group("##installed_details_rows");
        draw_value_row("Name", "The package's id.", package.name);
        draw_value_row("Version", "The version installed.", package.version);
        draw_value_row("Folder", "The folder below the data folder the package installs into.", package.root);
        draw_value_row("Installed From", "The package file it was installed from.", package.source_file);
        draw_value_row("Installed With", "The engine version that installed it.", package.installed_with);
        draw_value_row("Record", "Where the project records its packages. It belongs in version control.", packages::PACKAGE_REGISTRY_FILE_NAME);
    }
}

void packages_panel::draw_installed_files(const packages::installed_package& package)
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    if(!panel_section::draw_header("##installed_files", ICON_MDI_FILE_TREE, "Files", true).is_open)
    {
        return;
    }
    std::set<hpp::uuid> present;
    for(const auto& file : selected_state_.files)
    {
        present.insert(file.uid);
    }
    std::vector<packages_widgets::file_list_row> rows;
    for(const auto& file : selected_state_.files)
    {
        rows.push_back({packages::get_data_path(file.key),
                        {},
                        file.is_edited ? "Edited" : "Unchanged",
                        file.is_edited ? packages_widgets::COLOR_EDITED : packages_widgets::COLOR_MUTED_PILL,
                        false});
    }
    for(const auto& file : package.files)
    {
        if(present.count(file.uid) == 0)
        {
            rows.push_back({file.path, {}, "Deleted", packages_widgets::COLOR_ERROR, true});
        }
    }
    packages_widgets::file_list_parts parts;
    parts.label_width = FILE_STATUS_COLUMN;
    packages_widgets::draw_file_list("##installed_file_list",
                                     rows,
                                     installed_files_state_,
                                     parts,
                                     packages_widgets::to_pixels(FILES_MAX_HEIGHT),
                                     "The package has no file.");
}

void packages_panel::draw_library_details(rtti::context& ctx, const list_item& item)
{
    const auto& entry = *item.newest;
    const auto& manifest = entry.manifest;
    std::string byline = manifest.name + "   version " + manifest.version;
    if(!manifest.author.empty())
    {
        byline += "   by " + manifest.author;
    }
    draw_details_header(manifest.name, manifest.get_title(), byline, manifest.description, "Not installed", packages_widgets::COLOR_MUTED_PILL);
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    if(deploy_widgets::draw_button(ICON_MDI_PACKAGE_DOWN " Import...", true, true))
    {
        open_import(ctx, entry.file);
    }
    ImGui::SetItemTooltipEx("%s", "Review the import before anything is written.");
    ImGui::SameLine();
    if(deploy_widgets::draw_button(ICON_MDI_FOLDER_OPEN_OUTLINE " Show in Folder", false, true))
    {
        fs::show_in_graphical_env(entry.file);
    }
    ImGui::SetItemTooltipEx("%s", "Show the package file in the file browser.");
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const float gap = ImGui::GetFontSize() * 0.5f;
    const float width = packages_widgets::calc_tile_width(DETAILS_COUNTER_COUNT);
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    packages_widgets::draw_tile("Files", std::to_string(manifest.files.size()), text_color, width);
    ImGui::SameLine(0.0f, gap);
    packages_widgets::draw_tile("Size", format_bytes(entry.size, 1), text_color, width);
    ImGui::SameLine(0.0f, gap);
    packages_widgets::draw_tile("Versions", std::to_string(count_versions(library_, manifest.name)), text_color, width);
    ImGui::SameLine(0.0f, gap);
    const bool is_newer_engine = [&manifest]()
    {
        const auto made_with = version::parse(manifest.exported_with);
        return made_with && version::get_current() < *made_with;
    }();
    packages_widgets::draw_tile("Made With", manifest.exported_with, is_newer_engine ? packages_widgets::COLOR_EDITED : text_color, width);
    draw_library_files(manifest);
    if(!selected_readme_.empty() && panel_section::draw_header("##library_readme", ICON_MDI_TEXT_BOX_OUTLINE, "Read Me", true).is_open)
    {
        if(ImGui::BeginChild("##library_readme_text", ImVec2(0.0f, packages_widgets::to_pixels(packages_widgets::README_MAX_HEIGHT)), ImGuiChildFlags_AutoResizeY))
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(selected_readme_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }
    if(panel_section::draw_header("##library_details", ICON_MDI_INFORMATION_OUTLINE, "Details", false).is_open)
    {
        property_layout_group group("##library_details_rows");
        draw_value_row("Name", "The package's id.", manifest.name);
        draw_value_row("License", "The license the package is shared under.", manifest.license);
        draw_value_row("Minimum Engine", "The oldest engine version the package works with.", manifest.minimum_engine);
        draw_value_row("Folder", "The folder below the data folder it installs into.", manifest.root);
        draw_value_row("File", "The package file.", entry.file.generic_string());
    }
}

void packages_panel::draw_library_files(const packages::package_manifest& manifest)
{
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    const std::string title = fmt::format("Files ({})", manifest.files.size());
    if(!panel_section::draw_header("##library_files", ICON_MDI_FILE_TREE, title.c_str(), false).is_open)
    {
        return;
    }
    std::vector<packages_widgets::file_list_row> rows;
    rows.reserve(manifest.files.size());
    for(const auto& file : manifest.files)
    {
        rows.push_back({file.path, file.size});
    }
    packages_widgets::draw_file_list("##library_file_list",
                                     rows,
                                     library_files_state_,
                                     {},
                                     packages_widgets::to_pixels(FILES_MAX_HEIGHT),
                                     "The package has no file.");
}

void packages_panel::confirm_remove(rtti::context& ctx)
{
    const std::size_t edited = std::size_t(std::count_if(selected_state_.files.begin(),
                                                         selected_state_.files.end(),
                                                         [](const packages::planned_removal& file)
                                                         {
                                                             return file.is_edited;
                                                         }));
    const std::size_t trashed = selected_state_.files.size() - edited;
    const auto title = fmt::format("Remove {}?", selected_state_.package.get_title());
    const auto message = fmt::format("{} file(s) go to the project's trash, where they can be restored. {} edited file(s) stay in the "
                                     "project, no longer part of the package.\n\nScenes that use the removed files lose those "
                                     "references.",
                                     trashed,
                                     edited);
    ImBox::ShowConfirmation(title,
                            message,
                            [this, context = &ctx, name = selected_state_.package.name](ImBox::ModalResult result)
                            {
                                if(result == ImBox::ModalResult::Ok)
                                {
                                    remove_package(*context, name);
                                }
                            },
                            ImBox::MessageType::Warning);
}

void packages_panel::remove_package(rtti::context& ctx, const std::string& name)
{
    // The answer comes after the question: the project may have closed, or the package gone, meanwhile.
    const auto* item = find_item(name);
    if(!ctx.get_cached<project_manager>().has_open_project() || item == nullptr || item->installed == nullptr)
    {
        return;
    }
    const auto project = package_actions::make_project_view(ctx);
    const auto plan = packages::plan_removal(project, name);
    const auto result = package_actions::remove(ctx, plan, project);
    is_message_error_ = !result.is_done;
    message_ = result.is_done ? fmt::format("Removed {}: {} file(s) went to the trash and {} edited file(s) stayed.",
                                            plan.package.get_title(),
                                            result.removed_keys.size(),
                                            result.kept_count)
                              : "Removing the package failed and nothing changed: " + result.error;
    refresh(ctx);
}

void packages_panel::draw_message()
{
    if(message_.empty())
    {
        return;
    }
    deploy_widgets::draw_gap(packages_widgets::SECTION_GAP);
    deploy_widgets::draw_icon_line(is_message_error_ ? ICON_MDI_ALERT_OCTAGON : ICON_MDI_CHECK_CIRCLE,
                                   is_message_error_ ? packages_widgets::COLOR_ERROR : packages_widgets::COLOR_NEW,
                                   message_.c_str(),
                                   ImGui::GetColorU32(ImGuiCol_Text));
}

void packages_panel::refresh(rtti::context& ctx)
{
    is_refresh_pending_ = false;
    registry_ = package_actions::load_registry(ctx, registry_error_);
    library_ = packages::scan_library({package_actions::get_packages_folder()});
    installed_items_.clear();
    library_items_.clear();
    for(const auto& package : registry_.packages)
    {
        installed_items_.push_back({package.name, &package, packages::find_newest(library_, package.name)});
    }
    std::set<std::string> listed;
    for(const auto& entry : library_)
    {
        if(!entry.error.empty() || registry_.find(entry.manifest.name) != nullptr || !listed.insert(entry.manifest.name).second)
        {
            continue;
        }
        library_items_.push_back({entry.manifest.name, nullptr, packages::find_newest(library_, entry.manifest.name)});
    }
    if(find_item(selected_name_) == nullptr)
    {
        selected_name_ = !installed_items_.empty() ? installed_items_.front().name
                                                   : (!library_items_.empty() ? library_items_.front().name : std::string{});
    }
    select(ctx, selected_name_);
}

void packages_panel::select(rtti::context& ctx, const std::string& name)
{
    selected_name_ = name;
    selected_state_ = {};
    selected_readme_.clear();
    is_selected_authored_ = false;
    const auto* item = find_item(name);
    if(item == nullptr)
    {
        return;
    }
    if(item->installed != nullptr)
    {
        selected_state_ = packages::plan_removal(package_actions::make_project_view(ctx), name);
        is_selected_authored_ = is_authored_here(*item->installed);
        return;
    }
    packages::package_archive archive;
    std::string err;
    if(item->newest != nullptr && archive.open(item->newest->file, err))
    {
        selected_readme_ = archive.read_document(packages::PACKAGE_README_NAME);
    }
}

void packages_panel::pick_package_file(rtti::context& ctx)
{
    std::string picked;
    const std::string filter = std::string("*") + packages::PACKAGE_EXTENSION;
    fs::error_code ec;
    const auto packages_folder = package_actions::get_packages_folder();
    const auto start_folder = fs::is_directory(packages_folder, ec) ? packages_folder : packages_folder.parent_path();
    if(native::open_file_dialog(picked, {filter}, "Unravel packages", "Import Package", start_folder.string()))
    {
        open_import(ctx, fs::path(picked));
    }
}

void packages_panel::show_packages_folder()
{
    const auto folder = package_actions::get_packages_folder();
    fs::error_code ec;
    fs::create_directories(folder, ec);
    fs::show_in_graphical_env(folder);
}

auto packages_panel::find_item(const std::string& name) const -> const list_item*
{
    for(const auto* items : {&installed_items_, &library_items_})
    {
        for(const auto& item : *items)
        {
            if(item.name == name)
            {
                return &item;
            }
        }
    }
    return nullptr;
}

auto packages_panel::is_item_shown(const list_item& item) const -> bool
{
    if(!search_.IsActive())
    {
        return true;
    }
    const auto& title = item.installed != nullptr ? item.installed->get_title() : item.newest->manifest.get_title();
    const auto& author = item.installed != nullptr ? item.installed->author : item.newest->manifest.author;
    return search_.PassFilter(item.name.c_str()) || search_.PassFilter(title.c_str()) || search_.PassFilter(author.c_str());
}

auto packages_panel::is_authored_here(const packages::installed_package& package) const -> bool
{
    if(package.root.empty())
    {
        return false;
    }
    packages::package_manifest definition;
    std::string err;
    fs::error_code ec;
    const auto path = packages::get_definition_path(fs::resolve_protocol("app:/"), package.root);
    return fs::exists(path, ec) && packages::load_manifest(path, definition, err) && definition.name == package.name;
}

} // namespace unravel
