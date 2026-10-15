#pragma once

#include "sample_catalog.h"

#include <context/context.hpp>
#include <editor/imgui/image_file_cache.h>
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
 * A sidebar leads to the recent projects, the sample projects and the documentation, and keeps a
 * sample in view. The projects show as a grid of thumbnails or as a list, with search, sorting,
 * the keyboard and a context menu; pages to create a project and to remove one open in place.
 * Every size comes from the room there is and the font, so the page follows the window and the UI
 * scale.
 */
class start_page
{
public:
    void draw(rtti::context& ctx);
    /// Lets go of the images; called before the renderer shuts down.
    void release();

private:
    enum class view_state
    {
        projects,
        samples,
        learn,
        create_project,
        remove_project
    };

    enum class layout_mode
    {
        grid,
        list
    };

    enum class sort_mode
    {
        recent,
        name
    };

    /// What the page shows of one recent project. Read from disk by refresh_projects(), not
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
        /// Empty while the project has no thumbnail.
        fs::path thumbnail;
        /// Saved by an older engine than the running one: opening it asks first.
        bool is_engine_older{};
        bool is_missing{};
    };

    struct item_state
    {
        bool is_selected{};
        bool is_interactive{true};
    };

    // Projects data, start_page.cpp.
    /// Re-reads the recent projects when the list changed, the page came back, or the labels aged.
    void refresh_projects_if_stale(project_manager& pm);
    void refresh_projects(project_manager& pm);
    auto find_project(const std::string& path) const -> const project_entry*;
    /// Indices into projects_ of the entries that pass the search, in the order of the sort.
    auto collect_shown_projects() const -> std::vector<size_t>;

    // Actions, start_page.cpp.
    void show_view(view_state view);
    /// Opens the project, after a confirmation when its folder or its engine version is suspicious.
    void open_project(rtti::context& ctx, const std::string& path);
    void browse_for_project(rtti::context& ctx);
    void remove_from_recents(project_manager& pm, const std::string& path);
    /// Lets the user pick a PNG and makes it the thumbnail of the project.
    void pick_thumbnail(project_manager& pm, const std::string& path);

    // Shell, start_page.cpp.
    /// Ctrl+1 / 2 / 3 go to Projects / Samples / Learn, Ctrl+N creates a project, Ctrl+O opens one.
    void handle_shortcuts(rtti::context& ctx);
    void draw_sidebar(rtti::context& ctx, const ImVec2& size);
    void draw_brand();
    auto draw_nav_item(const char* id,
                       const char* icon,
                       const char* label,
                       const std::string& badge,
                       const char* tooltip,
                       bool is_selected,
                       bool is_highlighted) -> bool;
    /// One sample at a time, turning over, at the foot of the sidebar when there is room.
    void draw_featured_sample(rtti::context& ctx);
    void draw_main(rtti::context& ctx);
    auto get_sample_image(project_manager& pm, const sample_catalog::sample_info& sample) -> gfx::texture::ptr;

    // Projects view, start_page_projects.cpp.
    void draw_projects_view(rtti::context& ctx);
    void draw_projects_header(rtti::context& ctx);
    void draw_projects_toolbar();
    void draw_projects_content(rtti::context& ctx);
    void draw_projects_grid(rtti::context& ctx, const std::vector<size_t>& shown_projects);
    void draw_projects_list(rtti::context& ctx, const std::vector<size_t>& shown_projects);
    void draw_project_card(const project_entry& entry, const ImVec2& size, const item_state& state);
    void draw_project_row(const project_entry& entry, const item_state& state);
    void draw_project_image(ImDrawList* draw_list, const project_entry& entry, const ImRect& rect, float rounding, ImDrawFlags corners);
    void draw_project_item(rtti::context& ctx, const project_entry& entry, const ImVec2& card_size);
    void handle_project_item_input(rtti::context& ctx, const project_entry& entry);
    void draw_project_context_menu(rtti::context& ctx, const project_entry& entry);
    /// Arrows move the selection (Up / Down by a row of the grid), Enter opens it.
    void handle_projects_keys(rtti::context& ctx, const std::vector<size_t>& shown_projects, int columns);
    void draw_no_projects();

    // Samples and Learn views, start_page_samples.cpp.
    void draw_samples_view(rtti::context& ctx);
    void draw_sample_card(rtti::context& ctx, const sample_catalog::sample_info& sample, const ImVec2& size);
    void draw_learn_view();

    // Pages that replace the main area, start_page_forms.cpp.
    /// Header of a page with a way back; true when the user wants back, by the button or by Escape.
    auto draw_sub_page_header(const char* title, const char* subtitle) -> bool;
    void draw_create_project_view(rtti::context& ctx);
    void draw_remove_project_view(rtti::context& ctx);

    view_state view_{view_state::projects};
    layout_mode layout_{layout_mode::grid};
    sort_mode sort_{sort_mode::recent};
    std::vector<project_entry> projects_{};
    image_file_cache images_{};
    ImGuiTextFilter filter_{};
    std::string selected_project_{};
    /// The keys moved the selection: the list scrolls to it once.
    bool scrolls_to_selection_{};
    /// Ctrl+F was pressed: the search takes the keyboard on the next frame.
    bool focuses_search_{};
    /// Asked for from a context menu, carried out once the list is through.
    std::string pending_recent_removal_{};
    std::string project_to_remove_{};
    std::string new_project_name_{};
    std::string new_project_directory_{};
    /// When the entries were read, and the last frame the page was drawn in.
    double refresh_time_{-1.0};
    int last_drawn_frame_{-1};
};
} // namespace unravel
