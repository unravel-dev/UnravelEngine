#include "deploy_content.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/assets/impl/asset_references.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <unordered_map>

namespace unravel
{
namespace
{
/// The project settings the game loads first: the startup scene and the splash logos are handles there.
constexpr const char* DEPLOY_SETTINGS_KEY = "app:/settings/settings.cfg";
constexpr const char* DEPLOY_SETTINGS_NAME = "Project Settings";
/// The protocol a deploy selects content of; engine assets always ship whole.
constexpr const char* DEPLOY_CONTENT_PROTOCOL = "app";
/// A file holding a zero byte in its first bytes is binary and has no references to read.
constexpr size_t DEPLOY_TEXT_SNIFF_BYTES = 4096;
/// Larger text files (a huge glTF, a data dump) are not read for references.
constexpr std::uintmax_t DEPLOY_MAX_SCANNED_BYTES = std::uintmax_t(64) << 20;

/// Whether a rule on `rule_path` covers `key`: the path itself, or a folder above it.
auto is_covered_by(const std::string& key, const std::string& rule_path) -> bool
{
    if(rule_path.empty() || key.size() < rule_path.size() || key.compare(0, rule_path.size(), rule_path) != 0)
    {
        return false;
    }
    return key.size() == rule_path.size() || key[rule_path.size()] == '/' || rule_path.back() == '/';
}

/// The length of the longest rule path in `rules` that covers `key`; 0 when none does.
auto find_closest_rule_length(const std::vector<std::string>& rules, const std::string& key) -> size_t
{
    size_t length = 0;
    for(const auto& rule : rules)
    {
        if(is_covered_by(key, rule))
        {
            length = std::max(length, rule.size());
        }
    }
    return length;
}

void erase_rules_covered_by(std::vector<std::string>& rules, const std::string& key)
{
    rules.erase(std::remove_if(rules.begin(),
                               rules.end(),
                               [&key](const std::string& rule)
                               {
                                   return is_covered_by(rule, key);
                               }),
                rules.end());
}

/// Whether every asset at or under `key` ships as `is_shipped` wants.
auto is_everything_under(const deploy_settings& params, const deploy_content& content, const std::string& key, bool is_shipped)
    -> bool
{
    return std::all_of(content.entries.begin(),
                       content.entries.end(),
                       [&](const deploy_content_entry& entry)
                       {
                           return !is_covered_by(entry.key, key) || is_deploy_content_shipped(params, entry) == is_shipped;
                       });
}

/// Reads a text file whole; false for a binary, missing or oversized file.
auto read_text_file(const fs::path& file, std::string& text) -> bool
{
    fs::error_code ec;
    const auto size = fs::file_size(file, ec);
    if(ec || size > DEPLOY_MAX_SCANNED_BYTES)
    {
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if(!stream)
    {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    const size_t sniffed = std::min(text.size(), DEPLOY_TEXT_SNIFF_BYTES);
    return text.find('\0') >= sniffed;
}

auto get_compiled_size(const std::string& location) -> std::uint64_t
{
    std::uint64_t size = 0;
    for(const auto& path : get_deploy_compiled_paths(location))
    {
        fs::error_code ec;
        const auto file_size = fs::file_size(path, ec);
        size += ec ? 0 : file_size;
    }
    return size;
}

auto get_key_without_extension(const std::string& key) -> std::string
{
    const size_t slash = key.rfind('/');
    const size_t dot = key.rfind('.');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? key : key.substr(0, dot);
}

/// Breadth-first walk over the project's assets from the deploy's roots.
class content_walker
{
public:
    content_walker(asset_manager& am, deploy_content& content) : content_(content)
    {
        index_database(am.get_database_snapshot(std::string(DEPLOY_CONTENT_PROTOCOL) + ":/"));
    }

    /// A root: a scene of the deploy.
    void reach_scene(const std::string& key)
    {
        if(!reach(key, deploy_content_reason::scene, {}) && entry_by_key_.find(key) == entry_by_key_.end())
        {
            content_.missing_scenes.push_back(key);
        }
    }

    /// A root: the handles the project settings hold (splash logos and the like).
    void reach_settings()
    {
        std::string text;
        if(read_text_file(fs::resolve_protocol(DEPLOY_SETTINGS_KEY), text))
        {
            reach_uids(text, DEPLOY_SETTINGS_NAME);
        }
    }

    /// Roots: what the project's scripts name by path. A path naming a folder is listed instead,
    /// since what a script loads from there cannot be followed.
    void reach_script_paths()
    {
        for(const auto& script_key : script_keys_)
        {
            std::string text;
            if(!read_text_file(fs::resolve_protocol(script_key), text))
            {
                continue;
            }
            for(const auto& path : asset_references::find_quoted_protocol_paths(text, DEPLOY_CONTENT_PROTOCOL))
            {
                if(!reach_path(path, deploy_content_reason::named_in_script, script_key))
                {
                    add_named_folder(path);
                }
            }
        }
    }

    /// Follows the references of everything reached until nothing new is.
    void walk()
    {
        while(!queue_.empty())
        {
            const size_t index = queue_.front();
            queue_.pop_front();
            scan(content_.entries[index].key);
        }
    }

private:
    void index_database(const asset_database::database_t& database)
    {
        for(const auto& [uid, meta] : database)
        {
            if(is_deploy_script_location(meta.location))
            {
                script_keys_.push_back(meta.location);
                continue;
            }
            if(get_deploy_compiled_paths(meta.location).empty())
            {
                continue;
            }
            content_.entries.push_back({meta.location, false, deploy_content_reason::unused, {}, get_compiled_size(meta.location)});
            key_by_uid_.emplace(uid, meta.location);
            uid_by_key_.emplace(meta.location, uid);
            if(meta.meta.generated.is_generated())
            {
                generated_by_source_.emplace(meta.meta.generated.source_uid, meta.location);
            }
        }
        std::sort(content_.entries.begin(),
                  content_.entries.end(),
                  [](const deploy_content_entry& lhs, const deploy_content_entry& rhs)
                  {
                      return lhs.key < rhs.key;
                  });
        for(size_t i = 0; i < content_.entries.size(); ++i)
        {
            entry_by_key_.emplace(content_.entries[i].key, i);
            entries_by_stem_.emplace(get_key_without_extension(content_.entries[i].key), i);
        }
        std::sort(script_keys_.begin(), script_keys_.end());
    }

    /// Marks the asset used and queues it. False when it is not a shippable asset or was reached before.
    auto reach(const std::string& key, deploy_content_reason use, const std::string& used_by) -> bool
    {
        const auto found = entry_by_key_.find(key);
        if(found == entry_by_key_.end())
        {
            return false;
        }
        reach_index(found->second, use, used_by);
        return true;
    }

    void reach_index(size_t index, deploy_content_reason use, const std::string& used_by)
    {
        auto& entry = content_.entries[index];
        if(entry.is_used)
        {
            return;
        }
        entry.is_used = true;
        entry.use = use;
        entry.used_by = used_by;
        queue_.push_back(index);
    }

    /// An asset path as written: an exact key, else a key without its extension, as the asset
    /// manager resolves one. False when it names no asset.
    auto reach_path(const std::string& path, deploy_content_reason use, const std::string& used_by) -> bool
    {
        if(reach(path, use, used_by))
        {
            return true;
        }
        const auto [first, last] = entries_by_stem_.equal_range(path);
        for(auto it = first; it != last; ++it)
        {
            reach_index(it->second, use, used_by);
        }
        return first != last;
    }

    void reach_uids(hpp::string_view text, const std::string& used_by)
    {
        for(const auto& uid : asset_references::find_quoted_uids(text))
        {
            const auto found = key_by_uid_.find(uid);
            if(found != key_by_uid_.end())
            {
                reach(found->second, deploy_content_reason::referenced, used_by);
            }
        }
    }

    void add_named_folder(std::string path)
    {
        while(!path.empty() && path.back() == '/')
        {
            path.pop_back();
        }
        const std::string data_root = ex::get_data_directory(DEPLOY_CONTENT_PROTOCOL);
        const std::string prefix = path + "/";
        const bool is_folder = std::any_of(content_.entries.begin(),
                                           content_.entries.end(),
                                           [&prefix](const deploy_content_entry& entry)
                                           {
                                               return entry.key.compare(0, prefix.size(), prefix) == 0;
                                           });
        const bool is_listed =
            std::find(content_.named_folders.begin(), content_.named_folders.end(), path) != content_.named_folders.end();
        if(is_folder && path.size() > data_root.size() && !is_listed)
        {
            content_.named_folders.push_back(path);
        }
    }

    /// The references of one reached asset: uids and asset paths in its text, the files a UI
    /// document or style sheet links, and the assets its import writes beside it.
    void scan(const std::string& key)
    {
        std::string text;
        if(read_text_file(fs::resolve_protocol(key), text))
        {
            reach_uids(text, key);
            for(const auto& path : asset_references::find_quoted_protocol_paths(text, DEPLOY_CONTENT_PROTOCOL))
            {
                reach_path(path, deploy_content_reason::referenced, key);
            }
            if(is_ui_file(key))
            {
                reach_ui_links(key, text);
            }
        }
        const auto uid = find_uid(key);
        const auto [first, last] = generated_by_source_.equal_range(uid);
        for(auto it = first; it != last; ++it)
        {
            reach(it->second, deploy_content_reason::referenced, key);
        }
    }

    static auto is_ui_file(const std::string& key) -> bool
    {
        const auto extension = fs::path(key).extension().generic_string();
        return ex::is_format<ui_tree>(extension) || ex::is_format<style_sheet>(extension);
    }

    void reach_ui_links(const std::string& key, const std::string& text)
    {
        const auto folder = fs::resolve_protocol(key).parent_path();
        for(const auto& link : asset_references::find_ui_linked_paths(text))
        {
            if(fs::has_known_protocol(link))
            {
                reach_path(link, deploy_content_reason::referenced, key);
                continue;
            }
            const auto linked_key = fs::convert_to_protocol((folder / link).lexically_normal()).generic_string();
            reach_path(linked_key, deploy_content_reason::referenced, key);
        }
    }

    auto find_uid(const std::string& key) const -> hpp::uuid
    {
        const auto found = uid_by_key_.find(key);
        return found == uid_by_key_.end() ? hpp::uuid{} : found->second;
    }

    deploy_content& content_;
    std::unordered_map<std::string, size_t> entry_by_key_;
    std::unordered_multimap<std::string, size_t> entries_by_stem_;
    std::map<hpp::uuid, std::string> key_by_uid_;
    std::unordered_map<std::string, hpp::uuid> uid_by_key_;
    std::multimap<hpp::uuid, std::string> generated_by_source_;
    std::vector<std::string> script_keys_;
    std::deque<size_t> queue_;
};
} // namespace

auto is_deploy_script_location(const std::string& location) -> bool
{
    return ex::is_format<script>(fs::path(location).extension().generic_string());
}

auto get_deploy_compiled_paths(const std::string& location) -> std::vector<fs::path>
{
    if(location.find(ex::get_data_directory()) == std::string::npos || is_deploy_script_location(location))
    {
        return {};
    }
    const auto compiled = asset_reader::resolve_compiled_path(location);
    if(!ex::is_format<gfx::shader>(fs::path(location).extension().generic_string()))
    {
        return {compiled};
    }
    std::vector<fs::path> paths;
    for(const auto& renderer_extension : gfx::get_renderer_platform_supported_filename_extensions())
    {
        auto path = compiled;
        path.concat(renderer_extension);
        paths.push_back(std::move(path));
    }
    return paths;
}

auto get_deploy_source_key(const std::string& protocol, const fs::path& compiled_relative) -> std::string
{
    return ex::get_data_directory(protocol) + "/" + ex::get_source_path_from_compiled(compiled_relative).generic_string();
}

auto find_deploy_content_rule(const deploy_settings& params, const std::string& key) -> deploy_content_rule
{
    const size_t include_length = find_closest_rule_length(params.deploy_included, key);
    const size_t exclude_length = find_closest_rule_length(params.deploy_excluded, key);
    if(include_length == 0 && exclude_length == 0)
    {
        return deploy_content_rule::none;
    }
    return include_length > exclude_length ? deploy_content_rule::include : deploy_content_rule::exclude;
}

auto is_deploy_content_shipped(const deploy_settings& params, const deploy_content_entry& entry) -> bool
{
    switch(find_deploy_content_rule(params, entry.key))
    {
        case deploy_content_rule::include:
            return true;
        case deploy_content_rule::exclude:
            return false;
        case deploy_content_rule::none:
        default:
            return !params.deploy_only_used_assets || entry.is_used;
    }
}

auto get_deploy_content_reason(const deploy_settings& params, const deploy_content_entry& entry)
    -> deploy_content_reason
{
    switch(find_deploy_content_rule(params, entry.key))
    {
        case deploy_content_rule::include:
            return deploy_content_reason::included;
        case deploy_content_rule::exclude:
            return deploy_content_reason::excluded;
        case deploy_content_rule::none:
        default:
            return entry.use;
    }
}

void set_deploy_content_shipped(deploy_settings& params,
                                const deploy_content& content,
                                const std::string& key,
                                bool is_shipped)
{
    reset_deploy_content_rules(params, key);
    if(is_everything_under(params, content, key, is_shipped))
    {
        return;
    }
    (is_shipped ? params.deploy_included : params.deploy_excluded).push_back(key);
}

void reset_deploy_content_rules(deploy_settings& params, const std::string& key)
{
    erase_rules_covered_by(params.deploy_included, key);
    erase_rules_covered_by(params.deploy_excluded, key);
}

auto is_deploy_content_filtered(const deploy_settings& params) -> bool
{
    return params.deploy_only_used_assets || !params.deploy_excluded.empty();
}

auto get_deploy_left_out_keys(const deploy_settings& params, const deploy_content& content)
    -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> left_out;
    for(const auto& entry : content.entries)
    {
        if(!is_deploy_content_shipped(params, entry))
        {
            left_out.insert(entry.key);
        }
    }
    return left_out;
}

auto detect_deploy_content(asset_manager& am, const deploy_settings& params, const std::string& startup_scene)
    -> deploy_content
{
    deploy_content content;
    content_walker walker(am, content);
    if(!startup_scene.empty())
    {
        walker.reach_scene(startup_scene);
    }
    for(const auto& scene : params.deploy_scenes)
    {
        walker.reach_scene(scene);
    }
    walker.reach_settings();
    walker.reach_script_paths();
    walker.walk();
    return content;
}

} // namespace unravel
