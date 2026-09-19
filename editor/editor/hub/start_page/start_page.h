#pragma once

#include <context/context.hpp>
#include <editor/imgui/integration/imgui.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

namespace unravel
{
class project_manager;

/**
 * @brief The screen the editor shows while no project is open.
 *
 * The recent projects with the actions on them, and the pages to create a project, to remove one
 * and to browse the samples. Every width comes from the room there is, so the page follows the
 * window and the metrics of the theme.
 */
class start_page
{
public:
    void draw(rtti::context& ctx);

private:
    enum class view_state
    {
        projects,
        create_project,
        remove_project,
        samples
    };

    /// What the list shows of one recent project. Read from disk by refresh_projects(), not
    /// every frame.
    struct project_entry
    {
        /// As the recent projects list spells it; the key of the entry.
        std::string path;
        std::string name;
        std::string directory;
        std::string engine_version;
        /// "3 hours ago", or that the folder is gone.
        std::string modified_label;
        /// Saved by an older engine than the running one: opening it asks first.
        bool is_engine_older{};
        bool is_missing{};
    };

    struct project_row_state
    {
        bool is_selected{};
        bool is_interactive{true};
    };

    /// Re-reads the recent projects when the list changed, the page came back, or the labels aged.
    void refresh_projects_if_stale(project_manager& pm);
    void refresh_projects(project_manager& pm);
    auto find_project(const std::string& path) const -> const project_entry*;
    /// Indices into projects_ of the entries that pass the search.
    auto collect_shown_projects() const -> std::vector<size_t>;

    void show_view(view_state view);
    /// Opens the project, after a confirmation when its folder or its engine version is suspicious.
    void open_project(rtti::context& ctx, const std::string& path);
    void remove_from_recents(project_manager& pm, const std::string& path);

    void draw_projects_view(rtti::context& ctx);
    /// Heading with the count on the left, the search at the right end of width.
    void draw_projects_toolbar(float width);
    void draw_projects_list(rtti::context& ctx, const ImVec2& size);
    /// Returns true while the row is hovered.
    auto draw_project_row(const project_entry& entry, const project_row_state& state) -> bool;
    void handle_project_row_input(rtti::context& ctx, const project_entry& entry);
    void draw_project_context_menu(rtti::context& ctx, const project_entry& entry);
    void handle_list_keys(rtti::context& ctx, const std::vector<size_t>& shown_projects);
    void draw_actions(rtti::context& ctx, float width);

    void draw_create_project_view(rtti::context& ctx);
    void draw_remove_project_view(rtti::context& ctx);
    void draw_samples_view();

    view_state view_{view_state::projects};
    std::vector<project_entry> projects_{};
    ImGuiTextFilter filter_{};
    std::string selected_project_{};
    /// The keys moved the selection: the list scrolls to it once.
    bool scrolls_to_selection_{};
    /// Asked for from a row's context menu, carried out once the list is through.
    std::string pending_recent_removal_{};
    std::string project_to_remove_{};
    std::string new_project_name_{};
    std::string new_project_directory_{};
    /// When the entries were read, and the last frame the page was drawn in.
    double refresh_time_{-1.0};
    int last_drawn_frame_{-1};
};
} // namespace unravel
