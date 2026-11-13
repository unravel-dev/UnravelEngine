#pragma once

#include "packages_widgets.h"

#include <context/context.hpp>
#include <engine/threading/threader.h>
#include <packages/package_install.h>

#include <hpp/optional.hpp>
#include <imgui/imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The Packages window's review of a package before it is imported: what it is, whether this engine takes
 * it, what happens to each of its files (new, update, edited by you, clashes) with a choice where there is one,
 * and the button that applies it. After the import it says what was done. Planning and importing run in the
 * background; the import plans again first and stops when the project changed since the review was made.
 */
class package_import_view
{
public:
    /// Opens the package and starts planning its import. An error is shown in the view.
    void open(rtti::context& ctx, const fs::path& package_file);
    /// Closes the review. A running import is waited for first: it writes the project it was planned for, so it
    /// ends before that project can close.
    void close();
    auto is_open() const -> bool
    {
        return is_open_;
    }
    /// An import is writing the project: the review stays open until it is done.
    auto is_busy() const -> bool
    {
        return is_applying_;
    }
    /// The package's name while a review is open.
    auto get_package_name() const -> const std::string&;
    /// Draws the review into the rest of the window. @return False once the user closes it.
    auto draw(rtti::context& ctx) -> bool;

private:
    /// What applying the plan came to, from the background job.
    struct apply_outcome
    {
        /// The project changed since the plan was made: `plan` is the plan made now, and nothing was written.
        bool is_plan_changed{};
        packages::import_plan plan;
        packages::project_view project;
        packages::install_result result;
    };

    /// Reads the project on the main thread and plans the import in the background.
    void start_planning(rtti::context& ctx);
    void poll_jobs(rtti::context& ctx);
    void finish_apply(rtti::context& ctx, apply_outcome outcome);
    void draw_progress(const char* title, const std::string& text);
    void draw_open_error();
    void draw_header();
    void draw_notes();
    /// The counters of the files, which also filter the list.
    void draw_filters();
    void draw_files();
    void draw_file_toolbar();
    /// What happens to the planned file, as a dropdown when there is a choice.
    void draw_file_choice(std::size_t index);
    /// Whether the file the new version drops goes to the trash or stays.
    void draw_removal_choice(std::size_t index);
    void draw_file_tooltip(std::size_t index) const;
    void draw_removal_tooltip(std::size_t index) const;
    void draw_readme();
    /// @return False once the user closes the view.
    auto draw_footer(rtti::context& ctx) -> bool;
    auto draw_result(rtti::context& ctx) -> bool;
    void apply(rtti::context& ctx);
    auto is_file_shown(std::size_t index) const -> bool;
    auto is_removal_shown(std::size_t index) const -> bool;
    auto get_apply_label() const -> std::string;
    auto get_blocking_reason() const -> std::string;

    bool is_open_{};
    fs::path package_file_;
    std::string open_error_;
    /// Shared with the background jobs, which outlive a closed review.
    std::shared_ptr<const packages::package_archive> archive_;
    packages::project_view project_;
    tpp::shared_future<packages::import_plan> plan_job_;
    bool has_plan_{};
    packages::import_plan plan_;
    /// Problems the next plan starts with: why the last import did not go through.
    std::vector<std::string> next_plan_problems_;
    tpp::shared_future<apply_outcome> apply_job_;
    bool is_applying_{};
    std::string readme_;
    bool has_result_{};
    packages::install_result result_;
    ImGuiTextFilter search_;
    /// The counter the list shows only the files of; empty shows every file.
    hpp::optional<std::size_t> active_filter_;
    packages_widgets::file_list_state files_state_;
};

} // namespace unravel
