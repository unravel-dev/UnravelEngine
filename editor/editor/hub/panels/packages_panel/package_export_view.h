#pragma once

#include "packages_widgets.h"

#include <asset_compiler/asset_compiler.h>
#include <context/context.hpp>
#include <packages/package_export.h>
#include <engine/threading/threader.h>

#include <imgui/imgui.h>

#include <array>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The Packages window's export of a project folder: the package's name, version and the rest (saved in the
 * folder's package.json), every file that goes in and why, a build of the package's scripts on their own, and the
 * button that writes the .unravelpkg into the project's .packages folder.
 */
class package_export_view
{
public:
    /// Starts planning the export of the folder (a key such as "app:/data/UIKit") in the background.
    void open(rtti::context& ctx, const std::string& folder_key);
    void close();
    auto is_open() const -> bool
    {
        return is_open_;
    }
    /// Draws the export into the rest of the window. @return False once the user closes it.
    auto draw(rtti::context& ctx) -> bool;

private:
    /// What the script check found.
    struct script_check
    {
        bool builds{};
        std::vector<asset_compiler::script_compilation_entry> errors;
        /// Scripts the check added because the package's scripts use them, and the script types they define.
        std::vector<packages::export_entry> added_entries;
        std::vector<packages::script_type_source> added_types;
    };

    /**
     * @brief Builds the plan's scripts on their own; when the build misses types, adds the project scripts declaring
     * them and builds again, a few rounds at most. Runs in the background on its own copies.
     */
    static auto run_script_check(const packages::export_source& source, packages::export_plan plan) -> script_check;
    void poll_jobs();
    /// Puts the scripts a check added into the plan, unless the plan has them already.
    void add_found_scripts(const script_check& check);
    void start_script_check(rtti::context& ctx);
    void draw_planning();
    void draw_header();
    void draw_form();
    void draw_contents();
    void draw_contents_toolbar();
    /// The check box that puts the file into the package or leaves it out.
    void draw_entry_check(std::size_t index);
    void draw_entry_tooltip(std::size_t index) const;
    void draw_scripts();
    /// @return False once the user closes the view.
    auto draw_footer(rtti::context& ctx) -> bool;
    auto draw_result() -> bool;
    void export_package(rtti::context& ctx);
    auto get_blocking_reason() const -> std::string;
    auto get_output_file() const -> fs::path;
    /// The scenes the package holds, for the demo scene pick.
    auto get_scene_paths() const -> std::vector<std::string>;

    bool is_open_{};
    std::string folder_key_;
    /// What the plan was made from: the scripts a failed check misses are looked up in it.
    packages::export_source source_;
    /// Scripts the checks added because the package's scripts use them.
    std::size_t added_script_count_{};
    tpp::shared_future<packages::export_plan> plan_job_;
    bool has_plan_{};
    packages::export_plan plan_;
    packages::package_manifest definition_;
    tpp::shared_future<script_check> script_job_;
    bool has_script_check_{};
    /// A script was put in or left out: the check runs again.
    bool is_script_check_stale_{};
    script_check script_check_;
    std::string name_error_;
    std::string export_error_;
    bool is_exported_{};
    fs::path exported_file_;
    packages::package_manifest exported_;
    ImGuiTextFilter search_;
    packages_widgets::file_list_state files_state_;
};

} // namespace unravel
