#include "content.h"
#include "files.h"
#include "keys.h"
#include "profile.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_references.h>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>

namespace unravel::deployment
{
namespace
{
constexpr const char* PROJECT_SETTINGS_NAME = "Project Settings";
/// A file holding a zero byte in its first bytes is binary and has no references to read.
constexpr std::size_t TEXT_SNIFF_BYTES = 4096;
/// Larger text files are not read for references (logged): no asset file that references others comes
/// near it.
constexpr std::uintmax_t MAX_SCANNED_BYTES = std::uintmax_t(64) << 20;

/// The path below the protocol root of an app key ("app:/data/a.png" -> "data/a.png"); empty for other
/// protocols.
auto get_project_relative(const std::string& key) -> std::string
{
    const std::string prefix = std::string(CONTENT_PROTOCOL) + ":/";
    return key.compare(0, prefix.size(), prefix) == 0 ? key.substr(prefix.size()) : std::string{};
}

/// Reads a text file whole; false for a binary, missing or oversized file. The first bytes are read
/// first, so a binary source costs one small read.
auto read_scannable_text_file(const fs::path& file, std::string& text) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream)
    {
        return false;
    }
    std::array<char, TEXT_SNIFF_BYTES> head{};
    stream.read(head.data(), std::streamsize(head.size()));
    const auto head_size = std::size_t(stream.gcount());
    if(std::find(head.begin(), head.begin() + std::ptrdiff_t(head_size), '\0') != head.begin() + std::ptrdiff_t(head_size))
    {
        return false;
    }
    fs::error_code ec;
    if(fs::file_size(file, ec) > MAX_SCANNED_BYTES)
    {
        APPLOG_WARNING("Deploy content: {} is too large to read for references. What only it uses must be included "
                       "by a rule.",
                       file.generic_string());
        return false;
    }
    text.assign(head.data(), head_size);
    text.append(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

auto get_compiled_size(const std::string& key, const fs::path& project_root) -> std::uint64_t
{
    std::uint64_t size = 0;
    for(const auto& path : get_compiled_paths(key, project_root))
    {
        fs::error_code ec;
        const auto file_size = fs::file_size(path, ec);
        size += ec ? 0 : file_size;
    }
    return size;
}

/// The folder a script path that names no asset points into: before an interpolation ("{"), else its
/// parent folder ("app:/data/levels/level_" + n).
auto get_named_folder(const std::string& path, const content& found) -> std::string
{
    auto folder = normalize_key(path.substr(0, path.find('{')));
    if(!found.has_folder(folder))
    {
        const size_t slash = folder.rfind('/');
        folder = slash == std::string::npos ? std::string{} : folder.substr(0, slash);
    }
    return found.has_folder(folder) ? folder : std::string{};
}

/// Breadth-first walk over the project's assets from the deploy's roots.
class content_walker
{
public:
    content_walker(const content_source& source, content& result) : source_(source), content_(result)
    {
        index_database();
    }

    void reach_scene(const std::string& key)
    {
        if(!reach(key, content_reason::scene, {}))
        {
            content_.missing_scenes.push_back(key);
        }
    }

    /// The handles the shipped settings hold (splash logos and the like).
    void reach_settings()
    {
        if(!source_.settings_document.empty())
        {
            reach_uids(source_.settings_document, PROJECT_SETTINGS_NAME);
            return;
        }
        std::string text;
        if(read_scannable_text_file(get_file(PROJECT_SETTINGS_KEY), text))
        {
            reach_uids(text, PROJECT_SETTINGS_NAME);
        }
    }

    /// What the project's scripts name by path. A path that names no asset is reported as the folder it
    /// points into, since what a script loads from there cannot be followed.
    void reach_script_paths()
    {
        for(const auto& script_key : script_keys_)
        {
            std::string text;
            if(!read_scannable_text_file(get_file(script_key), text))
            {
                continue;
            }
            for(const auto& path : asset_references::find_quoted_protocol_paths(text, CONTENT_PROTOCOL))
            {
                if(!reach_path(path, content_reason::named_in_script, script_key))
                {
                    add_named_folder(get_named_folder(path, content_), script_key);
                }
            }
        }
    }

    void walk()
    {
        while(!queue_.empty())
        {
            const size_t index = queue_.front();
            queue_.pop_front();
            scan(index);
        }
    }

private:
    void index_database()
    {
        for(const auto& [uid, meta] : source_.database)
        {
            if(is_script_key(meta.location))
            {
                script_keys_.push_back(meta.location);
                continue;
            }
            if(get_compiled_paths(meta.location, source_.project_root).empty())
            {
                continue;
            }
            content_.entries.push_back({meta.location, false, content_reason::unused, {}, get_compiled_size(meta.location, source_.project_root)});
            if(meta.meta.generated.is_generated())
            {
                generated_by_source_.emplace(meta.meta.generated.source_uid, meta.location);
            }
        }
        std::sort(content_.entries.begin(),
                  content_.entries.end(),
                  [](const content_entry& lhs, const content_entry& rhs)
                  {
                      return lhs.key < rhs.key;
                  });
        content_.build_index();
        for(size_t i = 0; i < content_.entries.size(); ++i)
        {
            entries_by_stem_.emplace(fs::make_path_key(get_key_without_extension(content_.entries[i].key)), i);
        }
        for(const auto& [uid, meta] : source_.database)
        {
            key_by_uid_.emplace(uid, meta.location);
            uid_by_key_.emplace(fs::make_path_key(meta.location), uid);
        }
        std::sort(script_keys_.begin(), script_keys_.end());
    }

    auto get_file(const std::string& key) const -> fs::path
    {
        return source_.project_root / get_project_relative(key);
    }

    /// Marks the asset used and queues it. False when the key names no asset that can ship.
    auto reach(const std::string& key, content_reason use, const std::string& used_by) -> bool
    {
        const auto* entry = content_.find(key);
        if(entry == nullptr)
        {
            return false;
        }
        reach_index(std::size_t(entry - content_.entries.data()), use, used_by);
        return true;
    }

    void reach_index(std::size_t index, content_reason use, const std::string& used_by)
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

    /// An asset path as written: an exact key, else a key without its extension, as the asset manager
    /// resolves one. False when it names no asset.
    auto reach_path(const std::string& path, content_reason use, const std::string& used_by) -> bool
    {
        if(reach(path, use, used_by))
        {
            return true;
        }
        const auto [first, last] = entries_by_stem_.equal_range(fs::make_path_key(path));
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
                reach(found->second, content_reason::referenced, used_by);
            }
        }
    }

    void add_named_folder(const std::string& folder, const std::string& script_key)
    {
        if(folder.empty() || is_same_key(folder, ex::get_data_directory(CONTENT_PROTOCOL)))
        {
            return;
        }
        auto listed = std::find_if(content_.named_folders.begin(),
                                   content_.named_folders.end(),
                                   [&folder](const named_folder& candidate)
                                   {
                                       return is_same_key(candidate.key, folder);
                                   });
        if(listed == content_.named_folders.end())
        {
            content_.named_folders.push_back({folder, {}});
            listed = std::prev(content_.named_folders.end());
        }
        auto& scripts = listed->scripts;
        if(std::find(scripts.begin(), scripts.end(), script_key) == scripts.end())
        {
            scripts.push_back(script_key);
        }
    }

    /// The references of one reached asset: uids and asset paths in its text, the files a UI document
    /// or style sheet links, and the assets its import writes beside it.
    void scan(std::size_t index)
    {
        const std::string key = content_.entries[index].key;
        std::string text;
        if(read_scannable_text_file(get_file(key), text))
        {
            reach_uids(text, key);
            for(const auto& path : asset_references::find_quoted_protocol_paths(text, CONTENT_PROTOCOL))
            {
                reach_path(path, content_reason::referenced, key);
            }
            if(is_ui_file(key))
            {
                reach_ui_links(key, text);
            }
        }
        const auto uid = uid_by_key_.find(fs::make_path_key(key));
        if(uid == uid_by_key_.end())
        {
            return;
        }
        const auto [first, last] = generated_by_source_.equal_range(uid->second);
        for(auto it = first; it != last; ++it)
        {
            reach(it->second, content_reason::referenced, key);
        }
    }

    static auto is_ui_file(const std::string& key) -> bool
    {
        const auto extension = fs::path(key).extension().generic_string();
        return ex::is_format<ui_tree>(extension) || ex::is_format<style_sheet>(extension);
    }

    /// A link relative to the document's folder, or a protocol path, as a key; empty when it leaves the
    /// project.
    static auto resolve_link(const std::string& from_key, const std::string& link) -> std::string
    {
        if(link.find(":/") != std::string::npos)
        {
            return link;
        }
        const auto relative = (fs::path(get_project_relative(from_key)).parent_path() / link).lexically_normal().generic_string();
        const bool leaves_project = relative.empty() || relative.compare(0, 2, "..") == 0;
        return leaves_project ? std::string{} : std::string(CONTENT_PROTOCOL) + ":/" + relative;
    }

    void reach_ui_links(const std::string& key, const std::string& text)
    {
        for(const auto& link : asset_references::find_ui_linked_paths(text))
        {
            const auto linked_key = resolve_link(key, link);
            if(!linked_key.empty())
            {
                reach_path(linked_key, content_reason::referenced, key);
            }
        }
    }

    const content_source& source_;
    content& content_;
    std::unordered_multimap<std::string, std::size_t> entries_by_stem_;
    std::map<hpp::uuid, std::string> key_by_uid_;
    std::unordered_map<std::string, hpp::uuid> uid_by_key_;
    std::multimap<hpp::uuid, std::string> generated_by_source_;
    std::vector<std::string> script_keys_;
    std::deque<std::size_t> queue_;
};

/// The length of the longest rule in `rules` that covers `key`; 0 when none does.
auto find_closest_rule_length(const std::vector<std::string>& rules, const std::string& key) -> std::size_t
{
    std::size_t length = 0;
    for(const auto& rule : rules)
    {
        if(is_key_under(key, rule))
        {
            length = std::max(length, normalize_key(rule).size());
        }
    }
    return length;
}

void erase_rules_under(std::vector<std::string>& rules, const std::string& key)
{
    rules.erase(std::remove_if(rules.begin(),
                               rules.end(),
                               [&key](const std::string& rule)
                               {
                                   return is_key_under(rule, key);
                               }),
                rules.end());
}

/// Whether every asset at or under `key` ships as `is_wanted` says.
auto is_everything_under(const deploy_settings& params, const content& content, const std::string& key, bool is_wanted)
    -> bool
{
    return std::all_of(content.entries.begin(),
                       content.entries.end(),
                       [&](const content_entry& entry)
                       {
                           return !is_key_under(entry.key, key) || is_shipped(params, entry) == is_wanted;
                       });
}

/// The key as the project spells it: an entry's key, or the matching start of one under it.
auto spell_like_project(const std::string& key, const content& content) -> std::string
{
    if(const auto* entry = content.find(key))
    {
        return entry->key;
    }
    const auto found = std::find_if(content.entries.begin(),
                                    content.entries.end(),
                                    [&key](const content_entry& entry)
                                    {
                                        return is_key_under(entry.key, key);
                                    });
    return found == content.entries.end() ? key : found->key.substr(0, key.size());
}

/// Normalizes, respells and dedupes one key list; appends the keys that name nothing to `unknown`.
void normalize_key_list(std::vector<std::string>& keys, const content& content, std::vector<std::string>& unknown)
{
    std::vector<std::string> normalized;
    std::unordered_set<std::string> seen;
    for(const auto& key : keys)
    {
        const auto normal = normalize_key(key);
        if(!content.find(normal) && !content.has_folder(normal))
        {
            unknown.push_back(normal);
        }
        if(seen.insert(fs::make_path_key(normal)).second)
        {
            normalized.push_back(spell_like_project(normal, content));
        }
    }
    keys = std::move(normalized);
}

auto has_key(const std::vector<std::string>& keys, const std::string& key) -> bool
{
    return std::any_of(keys.begin(),
                       keys.end(),
                       [&key](const std::string& listed)
                       {
                           return is_same_key(listed, key);
                       });
}

auto make_content(const content_source& source) -> content
{
    content result;
    content_walker walker(source, result);
    return result;
}
} // namespace

auto content::find(const std::string& key) const -> const content_entry*
{
    const auto found = index_.find(fs::make_path_key(normalize_key(key)));
    return found == index_.end() ? nullptr : &entries[found->second];
}

auto content::has_folder(const std::string& key) const -> bool
{
    const auto folder = normalize_key(key);
    return !folder.empty() && std::any_of(entries.begin(),
                                          entries.end(),
                                          [&folder](const content_entry& entry)
                                          {
                                              return is_key_under(entry.key, folder) && !is_same_key(entry.key, folder);
                                          });
}

void content::build_index()
{
    index_.clear();
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        index_.emplace(fs::make_path_key(entries[i].key), i);
    }
}

auto make_project_content_source(asset_manager& am, const settings& shipped_settings) -> content_source
{
    const std::string root = std::string(CONTENT_PROTOCOL) + ":/";
    return {am.get_database_snapshot(root),
            fs::resolve_protocol(root),
            serialization::save_to_document(shipped_settings, SETTINGS_DOCUMENT_ROOT)};
}

auto detect_content(const content_source& source, const deploy_settings& params, const std::string& startup_scene)
    -> content
{
    content result;
    content_walker walker(source, result);
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
    return result;
}

auto list_content(const content_source& source) -> content
{
    return make_content(source);
}

auto find_rule(const deploy_settings& params, const std::string& key) -> content_rule
{
    const std::size_t include_length = find_closest_rule_length(params.deploy_included, key);
    const std::size_t exclude_length = find_closest_rule_length(params.deploy_excluded, key);
    if(include_length == 0 && exclude_length == 0)
    {
        return content_rule::none;
    }
    return include_length > exclude_length ? content_rule::include : content_rule::exclude;
}

auto has_own_rule(const deploy_settings& params, const std::string& key) -> bool
{
    return has_key(params.deploy_included, key) || has_key(params.deploy_excluded, key);
}

auto is_shipped(const deploy_settings& params, const content_entry& entry) -> bool
{
    switch(find_rule(params, entry.key))
    {
        case content_rule::include:
            return true;
        case content_rule::exclude:
            return false;
        case content_rule::none:
        default:
            return !params.deploy_only_used_assets || entry.is_used;
    }
}

auto get_reason(const deploy_settings& params, const content_entry& entry) -> content_reason
{
    switch(find_rule(params, entry.key))
    {
        case content_rule::include:
            return content_reason::included;
        case content_rule::exclude:
            return content_reason::excluded;
        case content_rule::none:
        default:
            return entry.use;
    }
}

auto passes_filter(const deploy_settings& params, const content_entry& entry, content_filter filter) -> bool
{
    switch(filter)
    {
        case content_filter::shipped:
            return is_shipped(params, entry);
        case content_filter::left_out:
            return !is_shipped(params, entry);
        case content_filter::changed:
            return find_rule(params, entry.key) != content_rule::none;
        case content_filter::all:
        default:
            return true;
    }
}

void set_shipped(deploy_settings& params, const content& content, const std::string& key, bool is_wanted)
{
    reset_rules(params, key);
    if(!is_everything_under(params, content, key, is_wanted))
    {
        (is_wanted ? params.deploy_included : params.deploy_excluded).push_back(spell_like_project(normalize_key(key), content));
    }
}

void reset_rules(deploy_settings& params, const std::string& key)
{
    erase_rules_under(params.deploy_included, key);
    erase_rules_under(params.deploy_excluded, key);
}

auto is_filtered(const deploy_settings& params) -> bool
{
    return params.deploy_only_used_assets || !params.deploy_excluded.empty();
}

auto needs_walk(const deploy_settings& params) -> bool
{
    return params.deploy_only_used_assets;
}

auto get_left_out_keys(const deploy_settings& params, const content& content) -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> left_out;
    for(const auto& entry : content.entries)
    {
        if(!is_shipped(params, entry))
        {
            left_out.insert(fs::make_path_key(entry.key));
        }
    }
    return left_out;
}

auto find_orphan_rules(const deploy_settings& params, const content& content) -> std::vector<std::string>
{
    std::vector<std::string> orphans;
    for(const auto* rules : {&params.deploy_included, &params.deploy_excluded})
    {
        std::copy_if(rules->begin(),
                     rules->end(),
                     std::back_inserter(orphans),
                     [&content](const std::string& rule)
                     {
                         return content.find(rule) == nullptr && !content.has_folder(rule);
                     });
    }
    return orphans;
}

auto normalize_keys(deploy_settings& params, const content& content) -> std::vector<std::string>
{
    std::vector<std::string> unknown;
    normalize_key_list(params.deploy_scenes, content, unknown);
    normalize_key_list(params.deploy_included, content, unknown);
    normalize_key_list(params.deploy_excluded, content, unknown);
    return unknown;
}

auto get_reason_name(content_reason reason) -> const char*
{
    switch(reason)
    {
        case content_reason::scene:
            return "scene";
        case content_reason::referenced:
            return "referenced";
        case content_reason::named_in_script:
            return "named_in_script";
        case content_reason::included:
            return "included";
        case content_reason::excluded:
            return "excluded";
        case content_reason::unused:
        default:
            return "unused";
    }
}

auto get_filter_name(content_filter filter) -> const char*
{
    switch(filter)
    {
        case content_filter::shipped:
            return "shipped";
        case content_filter::left_out:
            return "left_out";
        case content_filter::changed:
            return "changed";
        case content_filter::all:
        default:
            return "all";
    }
}

auto parse_filter(const std::string& name, content_filter& filter) -> bool
{
    constexpr std::array<content_filter, 4> filters = {content_filter::all,
                                                        content_filter::shipped,
                                                        content_filter::left_out,
                                                        content_filter::changed};
    const auto found = std::find_if(filters.begin(),
                                    filters.end(),
                                    [&name](content_filter candidate)
                                    {
                                        return name == get_filter_name(candidate);
                                    });
    if(found == filters.end())
    {
        return false;
    }
    filter = *found;
    return true;
}

} // namespace unravel::deployment
