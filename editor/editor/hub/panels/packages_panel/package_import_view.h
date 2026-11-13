#pragma once

#include <context/context.hpp>
#include <engine/threading/threader.h>
#include <packages/package_install.h>

#include <imgui/imgui.h>

#include <array>
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
    /// Closes the review; never while is_busy().
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
    /// The status groups the file list filters by.
    static constexpr std::size_t FILTER_COUNT = 5;

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
    void draw_counters();
    void draw_files();
    void draw_file_filters();
    void draw_file_row(std::size_t index, int row_index);
    void draw_removal_row(std::size_t index, int row_index);
    void draw_readme();
    /// @return False once the user closes the view.
    auto draw_footer(rtti::context& ctx) -> bool;
    auto draw_result(rtti::context& ctx) -> bool;
    void apply(rtti::context& ctx);
    auto is_file_shown(std::size_t index) const -> bool;
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
    std::array<bool, FILTER_COUNT> shown_groups_{true, true, true, true, true};
};

} // namespace unravel
