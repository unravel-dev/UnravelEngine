#include "content_tree.h"
#include "keys.h"

#include <engine/assets/impl/asset_extensions.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <unordered_map>

namespace unravel::deployment
{
namespace
{
/// Combines a value into a running hash.
void hash_combine(std::size_t& seed, std::size_t value)
{
    constexpr std::size_t golden_ratio = 0x9e3779b97f4a7c15ull;
    constexpr int left_shift = 6;
    constexpr int right_shift = 2;
    seed ^= value + golden_ratio + (seed << left_shift) + (seed >> right_shift);
}

auto make_signature(const deploy_settings& params, const tree_view& view, const content* found) -> std::size_t
{
    std::size_t seed = std::hash<const void*>{}(found);
    hash_combine(seed, params.deploy_only_used_assets ? 1 : 0);
    for(const auto* rules : {&params.deploy_included, &params.deploy_excluded})
    {
        hash_combine(seed, rules->size());
        for(const auto& rule : *rules)
        {
            hash_combine(seed, std::hash<std::string>{}(rule));
        }
    }
    hash_combine(seed, std::hash<std::string>{}(view.search));
    hash_combine(seed, std::size_t(view.filter));
    return seed;
}

void reset_counts(tree_node& node)
{
    node.asset_count = 0;
    node.shipped_count = 0;
    node.shipped_size = 0;
    node.total_size = 0;
    node.is_visible = false;
}

void add_child_counts(tree_node& node, const tree_node& child)
{
    node.asset_count += child.asset_count;
    node.shipped_count += child.shipped_count;
    node.total_size += child.total_size;
    node.shipped_size += child.shipped_size;
    node.has_rule_inside = node.has_rule_inside || child.has_rule_inside;
    node.is_visible = node.is_visible || child.is_visible;
}
} // namespace

void content_tree::build(const content& found)
{
    content_ = &found;
    nodes_.clear();
    signature_ = 0;
    const std::string root_key = ex::get_data_directory(CONTENT_PROTOCOL);
    nodes_.push_back({get_key_name(root_key), root_key});
    std::unordered_map<std::string, std::size_t> folders{{root_key, 0}};
    for(std::size_t i = 0; i < found.entries.size(); ++i)
    {
        const auto& key = found.entries[i].key;
        if(key.compare(0, root_key.size() + 1, root_key + "/") != 0)
        {
            continue;
        }
        std::size_t parent = 0;
        std::size_t start = root_key.size() + 1;
        for(std::size_t slash = key.find('/', start); slash != std::string::npos; slash = key.find('/', start))
        {
            const auto folder_key = key.substr(0, slash);
            auto folder = folders.find(folder_key);
            if(folder == folders.end())
            {
                nodes_.push_back({key.substr(start, slash - start), folder_key, -1, {}, nodes_[parent].depth + (parent == 0 ? 0 : 1)});
                folder = folders.emplace(folder_key, nodes_.size() - 1).first;
                nodes_[parent].children.push_back(folder->second);
            }
            parent = folder->second;
            start = slash + 1;
        }
        nodes_.push_back({get_key_name(key), key, int(i), {}, parent == 0 ? 0 : nodes_[parent].depth + 1});
        nodes_[parent].children.push_back(nodes_.size() - 1);
    }
    sort_children();
}

void content_tree::sort_children()
{
    for(auto& node : nodes_)
    {
        std::sort(node.children.begin(),
                  node.children.end(),
                  [this](std::size_t lhs, std::size_t rhs)
                  {
                      if(nodes_[lhs].is_folder() != nodes_[rhs].is_folder())
                      {
                          return nodes_[lhs].is_folder();
                      }
                      return string_utils::to_lower(nodes_[lhs].name) < string_utils::to_lower(nodes_[rhs].name);
                  });
    }
}

auto content_tree::is_entry_shown(const deploy_settings& params, const content_entry& entry, const tree_view& view) const -> bool
{
    if(!view.search.empty() &&
       string_utils::to_lower(get_display_path(entry.key)).find(string_utils::to_lower(view.search)) == std::string::npos)
    {
        return false;
    }
    return passes_filter(params, entry, view.filter);
}

/// Children come after their parent in nodes_, so one backward pass sums every folder.
void content_tree::update(const deploy_settings& params, const tree_view& view)
{
    const auto signature = make_signature(params, view, content_);
    if(content_ == nullptr || signature == signature_)
    {
        return;
    }
    signature_ = signature;
    is_narrowed_ = view.is_narrowed();
    left_out_used_count_ = 0;
    for(std::size_t i = nodes_.size(); i-- > 0;)
    {
        auto& node = nodes_[i];
        node.has_rule_inside = has_own_rule(params, node.key);
        reset_counts(node);
        if(!node.is_folder())
        {
            const auto& entry = content_->entries[std::size_t(node.entry)];
            const bool is_entry_shipped = is_shipped(params, entry);
            node.asset_count = 1;
            node.shipped_count = is_entry_shipped ? 1 : 0;
            node.total_size = entry.size;
            node.shipped_size = is_entry_shipped ? entry.size : 0;
            node.is_visible = is_entry_shown(params, entry, view);
            left_out_used_count_ += entry.is_used && !is_entry_shipped ? 1 : 0;
            continue;
        }
        for(const std::size_t child : node.children)
        {
            add_child_counts(node, nodes_[child]);
        }
    }
}

void content_tree::collect_rows(const std::function<bool(const tree_node&)>& is_open, std::vector<std::size_t>& rows) const
{
    rows.clear();
    if(is_built())
    {
        collect_rows_under(0, is_open, rows);
    }
}

void content_tree::collect_rows_under(std::size_t index,
                                      const std::function<bool(const tree_node&)>& is_open,
                                      std::vector<std::size_t>& rows) const
{
    for(const std::size_t child : nodes_[index].children)
    {
        const auto& node = nodes_[child];
        if(!node.is_visible)
        {
            continue;
        }
        rows.push_back(child);
        if(node.is_folder() && (is_narrowed_ || is_open(node)))
        {
            collect_rows_under(child, is_open, rows);
        }
    }
}

} // namespace unravel::deployment
