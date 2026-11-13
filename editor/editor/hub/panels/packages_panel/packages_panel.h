#pragma once

#include "package_export_view.h"
#include "package_import_view.h"

#include <context/context.hpp>
#include <packages/package_install.h>
#include <packages/package_library.h>
#include <packages/package_registry.h>

#include <imgui/imgui.h>

#include <deque>
#include <string>
#include <vector>

namespace unravel
{
class imgui_panels;

/**
 * @brief The Packages window: the packages the project has installed and the package files in its .packages folder,
 * the details of the selected one with what can be done to it (update, remove, import, export a new version),
 * and the review of an import or an export, which take the window's body while they are open.
 */
class packages_panel
{
public:
    explicit packages_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);
    void show(bool s);
    /// Shows the window with the review of importing the package file. A review already open (or an import running)
    /// goes first; this one follows once it is closed.
    void open_import(rtti::context& ctx, const fs::path& package_file);
    /// open_import for each file, one review after the other.
    void open_imports(rtti::context& ctx, const std::vector<fs::path>& package_files);
    /// Shows the window with the export of the project folder (a key such as "app:/data/UIKit").
    void open_export(rtti::context& ctx, const std::string& folder_key);

private:
    /// A row of the list: an installed package, or one the .packages folder holds and the project has not installed.
    struct list_item
    {
        std::string name;
        const packages::installed_package* installed{};
        /// The newest version the .packages folder holds.
        const packages::library_entry* newest{};
    };

    void draw_ui(rtti::context& ctx);
    void draw_toolbar(rtti::context& ctx);
    void draw_review_toolbar(const char* title);
    void draw_export_menu(rtti::context& ctx);
    void draw_browse(rtti::context& ctx);
    void draw_list();
    void draw_list_section(const char* id, const char* icon, const char* title, const std::vector<list_item>& items);
    void draw_list_row(const list_item& item, int row_index);
    void draw_details(rtti::context& ctx);
    void draw_installed_details(rtti::context& ctx, const list_item& item);
    void draw_library_details(rtti::context& ctx, const list_item& item);
    void draw_details_header(const std::string& name,
                             const std::string& title,
                             const std::string& byline,
                             const std::string& description,
                             const char* state,
                             ImU32 state_color);
    void draw_installed_files(const packages::installed_package& package);
    void draw_library_files(const packages::package_manifest& manifest);
    void draw_remove_popup(rtti::context& ctx);
    void draw_message();
    void refresh(rtti::context& ctx);
    void select(rtti::context& ctx, const std::string& name);
    void pick_package_file(rtti::context& ctx);
    /// Shows the .packages folder in the file browser, creating it when nothing was exported yet.
    void show_packages_folder();
    auto find_item(const std::string& name) const -> const list_item*;
    auto is_item_shown(const list_item& item) const -> bool;
    /// Whether the project holds the package's source folder (its package.json): it can export a new version.
    auto is_authored_here(const packages::installed_package& package) const -> bool;

    imgui_panels* parent_{};
    bool show_request_{};
    bool show_{};
    bool is_refresh_pending_{true};
    /// A row was clicked: the selection's state is read on the next frame.
    bool is_selection_pending_{};
    /// The selected installed package's source folder is in this project.
    bool is_selected_authored_{};
    packages::package_registry registry_;
    /// Why packages.cfg did not read; empty when it did. Imports and removals refuse to run meanwhile.
    std::string registry_error_;
    /// Package files waiting for their review, the next first.
    std::deque<fs::path> queued_imports_;
    std::vector<packages::library_entry> library_;
    std::vector<list_item> installed_items_;
    std::vector<list_item> library_items_;
    std::string selected_name_;
    /// The selected installed package's files as they are now: which are edited, which are gone.
    packages::removal_plan selected_state_;
    /// The read me of the selected package that is not installed.
    std::string selected_readme_;
    ImGuiTextFilter search_;
    package_import_view import_view_;
    package_export_view export_view_;
    bool is_remove_popup_requested_{};
    /// What the last action did, shown under the details.
    std::string message_;
    bool is_message_error_{};
};

} // namespace unravel
