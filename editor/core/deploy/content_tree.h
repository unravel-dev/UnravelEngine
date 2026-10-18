#pragma once

#include "content.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unravel::deployment
{

/// A folder or an asset of the content tree.
struct tree_node
{
    std::string name;
    /// The asset key, or the folder's key without a trailing slash.
    std::string key;
    /// The asset's index in content::entries; -1 for a folder.
    int entry{-1};
    /// Folders first, then assets, each by name.
    std::vector<std::size_t> children;
    /// Nesting below the data folder: 0 for its children.
    std::size_t depth{};
    // Recomputed by content_tree::update.
    std::size_t asset_count{};
    std::size_t shipped_count{};
    std::uint64_t shipped_size{};
    std::uint64_t total_size{};
    /// A rule names this node or one under it.
    bool has_rule_inside{};
    /// It or something under it passes the search and the filter.
    bool is_visible{};

    auto is_folder() const -> bool
    {
        return entry < 0;
    }
};

/// What the tree shows: assets whose display path holds `search` (letter case ignored) and pass `filter`.
struct tree_view
{
    std::string search;
    content_filter filter{content_filter::all};

    auto is_narrowed() const -> bool
    {
        return !search.empty() || filter != content_filter::all;
    }
};

/**
 * @brief The project's assets as a folder tree, counted and filtered by the deploy settings.
 *
 * Built once per detection; update is cheap to call every frame, recounting only when the settings or
 * the view changed.
 */
class content_tree
{
public:
    /// Builds the folders and assets of `found`, which must outlive the tree's use.
    void build(const content& found);
    void update(const deploy_settings& params, const tree_view& view);

    auto get_nodes() const -> const std::vector<tree_node>&
    {
        return nodes_;
    }

    /// The data folder; its children are the top rows.
    auto get_root() const -> const tree_node&
    {
        return nodes_.front();
    }

    auto is_built() const -> bool
    {
        return !nodes_.empty();
    }

    /// Assets the scenes use that a rule leaves out: the game may miss them.
    auto get_left_out_used_count() const -> std::size_t
    {
        return left_out_used_count_;
    }

    /**
     * @brief The visible rows in tree order, as node indices: a folder's children follow it when
     * `is_open` says so, and always while the view is narrowed (searching or filtering).
     */
    void collect_rows(const std::function<bool(const tree_node&)>& is_open, std::vector<std::size_t>& rows) const;

private:
    void sort_children();
    auto is_entry_shown(const deploy_settings& params, const content_entry& entry, const tree_view& view) const -> bool;
    void collect_rows_under(std::size_t index, const std::function<bool(const tree_node&)>& is_open, std::vector<std::size_t>& rows) const;

    const content* content_{};
    std::vector<tree_node> nodes_;
    bool is_narrowed_{};
    std::size_t left_out_used_count_{};
    /// What the counts were computed for; the same settings and view skip the recount.
    std::size_t signature_{};
};

} // namespace unravel::deployment
