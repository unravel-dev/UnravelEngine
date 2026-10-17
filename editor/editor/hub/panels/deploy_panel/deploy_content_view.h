#pragma once

#include <context/context.hpp>

#include <editor/deploy/deploy_content.h>
#include <engine/threading/threader.h>

#include <imgui/imgui.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The Deploy window's Content tab: the scenes the game is built from, and every project asset
 * in a folder tree with whether it ships and why.
 *
 * The tree comes from detect_deploy_content, run off the main thread when the tab first shows, when
 * the scenes change and on Detect. A checkbox sets an include or exclude rule on its folder or asset;
 * the rules are applied every frame, so the tree always shows what a deploy would ship.
 */
class deploy_content_view
{
public:
    void draw(rtti::context& ctx);
    /// Detects again the next time the tab shows: the window was reopened, the project may have changed.
    void invalidate();

private:
    /// A folder or an asset row of the tree.
    struct tree_node
    {
        std::string name;
        /// The asset key, or the folder's key without a trailing slash.
        std::string key;
        /// The asset's index in content_.entries; -1 for a folder.
        int entry{-1};
        /// Folders first, then assets, each by name.
        std::vector<size_t> children;
        // Recomputed every frame from the rules.
        size_t asset_count{};
        size_t shipped_count{};
        std::uint64_t shipped_size{};
        std::uint64_t total_size{};
        bool has_rule_inside{};
        bool is_visible{};
    };

    enum class row_filter
    {
        all,
        shipped,
        left_out,
        changed,
    };

    void draw_scenes(rtti::context& ctx);
    void draw_scene_row(rtti::context& ctx, const std::string& key, bool is_startup, std::string& removed);
    void draw_add_scene_button(rtti::context& ctx);
    void draw_assets_header(rtti::context& ctx);
    void draw_mode(rtti::context& ctx);
    void draw_toolbar(rtti::context& ctx);
    void draw_summary();
    void draw_notes(rtti::context& ctx);
    void draw_tree(rtti::context& ctx);
    void draw_node(rtti::context& ctx, size_t index);
    void draw_node_menu(rtti::context& ctx, const tree_node& node);
    void draw_status_cell(const tree_node& node, const deploy_settings& params);
    auto draw_ship_checkbox(const tree_node& node) -> bool;

    void start_detection(rtti::context& ctx);
    void poll_detection();
    void build_tree();
    void update_tree(const deploy_settings& params);
    auto is_row_shown(const tree_node& node, const deploy_settings& params) const -> bool;
    auto get_use_chain(const deploy_content_entry& entry) const -> std::string;
    void set_shipped(rtti::context& ctx, const std::string& key, bool is_shipped);
    void reset_rules(rtti::context& ctx, const std::string& key);

    tpp::shared_future<deploy_content> detect_job_;
    deploy_content content_;
    bool has_content_{};
    bool is_detection_requested_{true};
    /// The startup scene the content was detected from; another one makes it stale.
    std::string detected_startup_scene_;
    std::chrono::steady_clock::time_point detected_at_{};
    std::vector<tree_node> nodes_;
    ImGuiTextFilter search_;
    row_filter filter_{row_filter::all};
    /// The project's scenes, listed while the Add Scene picker is open.
    std::vector<std::string> scene_choices_;
    ImGuiTextFilter scene_search_;
};

} // namespace unravel
