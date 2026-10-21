#pragma once

#include <context/context.hpp>

#include "deploy_settings_editor.h"

#include <deploy/content.h>
#include <deploy/content_tree.h>
#include <engine/threading/threader.h>

#include <imgui/imgui.h>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace unravel
{

/**
 * @brief The Deploy window's Content tab: the scenes the game is built from, and every project asset in
 * a folder tree with whether it ships and why.
 *
 * The content comes from deployment::detect_content, run off the main thread whenever what it was detected
 * from (the project, the startup scene, the scene list) changes, when the window opens and on Detect.
 * A checkbox sets an include or exclude rule on its folder or asset; the tree model recounts only when
 * the rules or the view change, and only the visible rows are drawn.
 */
class deploy_content_view
{
public:
    /// Draws the content of the deploy settings `editor` shows, and edits them through it.
    void draw(rtti::context& ctx, deploy_settings_editor& editor);
    /// Detects again the next time the tab shows: the window was reopened, the project may have changed.
    void invalidate();

private:
    /// The startup scene of the deploy profile.
    auto get_shipped_settings(rtti::context& ctx) const -> settings;
    auto get_startup_scene(rtti::context& ctx) const -> std::string;
    void update_content(rtti::context& ctx);
    void start_detection(rtti::context& ctx, const std::string& signature);
    void poll_detection();

    void draw_scenes(rtti::context& ctx);
    void draw_scene_row(const std::string& key, bool is_startup, std::string& removed);
    void draw_scene_buttons(rtti::context& ctx);
    void draw_scene_picker(rtti::context& ctx);
    void add_scenes(rtti::context& ctx, const std::vector<std::string>& keys);

    void draw_assets_header(rtti::context& ctx);
    void draw_mode(rtti::context& ctx);
    void draw_toolbar(rtti::context& ctx);
    void draw_reset_menu(rtti::context& ctx);
    void draw_summary();
    void draw_notes(rtti::context& ctx);
    void draw_named_folder_notes(rtti::context& ctx);
    void draw_orphan_rule_note(rtti::context& ctx);

    void draw_tree(rtti::context& ctx);
    void draw_row(rtti::context& ctx, std::size_t index, float row_height);
    void draw_row_menu(rtti::context& ctx, const deployment::tree_node& node);
    void draw_status_cell(const deployment::tree_node& node, const deploy_settings& params) const;
    auto draw_ship_checkbox(const deployment::tree_node& node) const -> bool;
    void toggle_folder(const std::string& key);

    auto get_use_chain(const deployment::content_entry& entry) const -> std::string;
    void set_shipped(rtti::context& ctx, const std::string& key, bool is_shipped);
    void reset_rules(rtti::context& ctx, const std::string& key);

    tpp::shared_future<deployment::content> detect_job_;
    /// What the running job detects from.
    std::string detect_signature_;
    /// Kept at one address: the tree points into it.
    std::unique_ptr<deployment::content> content_;
    /// What content_ was detected from: the project root, the startup scene and the scene list.
    std::string content_signature_;
    std::string content_root_;
    std::chrono::steady_clock::time_point detected_at_{};
    bool is_invalidated_{true};

    /// The editor the frame's draw shows; set by draw.
    deploy_settings_editor* editor_{};
    deployment::content_tree tree_;
    deployment::tree_view view_;
    std::unordered_set<std::string> open_folders_;
    std::vector<std::size_t> rows_;
    ImGuiTextFilter search_;

    /// The search of the Add Scene picker.
    ImGuiTextFilter scene_search_;
};

} // namespace unravel
