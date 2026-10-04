#include "watcher_fallback.h"
#include "change_trigger.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <base/platform/thread.hpp>
#include <hpp/optional.hpp>

namespace fs
{
using namespace std::literals;

namespace
{
using clock_type = watcher::clock_t;
using path_key = fs::path::string_type;
using time_point = clock_type::time_point;

/// Quiet time after the last reported change before a scan, so a burst of writes is scanned once.
constexpr auto SETTLE_DELAY = 50ms;
/// Held entries wait this fraction of the poll interval for their other half. A delete reaches the asset
/// through three trees in turn (source, .meta, compiled), each holding it once, so the hold is kept short
/// enough for that chain to stay below one and a half poll intervals.
constexpr int HOLD_DIVISOR = 2;
/// Re-check interval while every watch is paused.
constexpr auto PAUSED_WAIT = 500ms;
/// Longest single wait of the polling thread; any wake-up request ends it early.
constexpr auto MAX_WAIT = 1h;

/// Absolute, lexically normal, without a trailing separator: the one spelling listeners are keyed by.
auto make_absolute(const fs::path& path) -> fs::path
{
    fs::error_code err;
    fs::path result = fs::absolute(path, err);
    if(err)
    {
        result = path;
    }
    result = result.lexically_normal();
    if(!result.has_filename() && result.has_relative_path())
    {
        result = result.parent_path();
    }
    return result;
}

auto has_same_extensions(const fs::path& lhs, const fs::path& rhs) -> bool
{
    bool same_extensions = true;
    auto lhs_stem = lhs;
    auto rhs_stem = rhs;
    while(lhs_stem.has_extension() || rhs_stem.has_extension())
    {
        same_extensions &= lhs_stem.extension() == rhs_stem.extension();
        lhs_stem = lhs_stem.stem();
        rhs_stem = rhs_stem.stem();
    }
    return same_extensions;
}

/// The path new_path had before its ancestor renamed_path was renamed from old_path.
auto get_original_path(const fs::path& old_path, const fs::path& renamed_path, const fs::path& new_path) -> fs::path
{
    return old_path / new_path.lexically_relative(renamed_path);
}

template<typename Iterator, typename Visitor>
auto walk_with(const fs::path& root, Visitor& visit) -> bool
{
    fs::error_code err;
    Iterator it(root, fs::directory_options::skip_permission_denied, err);
    if(err)
    {
        fs::error_code exists_err;
        return !fs::exists(root, exists_err) && !exists_err;
    }
    const Iterator end;
    while(it != end)
    {
        visit(*it);
        it.increment(err);
        if(err)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Visits every entry under root.
 * @return False when the walk stopped on an error before the end. A root that does not exist is a
 * complete, empty walk.
 */
template<typename Visitor>
auto walk_directory(const fs::path& root, bool recursive, Visitor&& visit) -> bool
{
    if(recursive)
    {
        return walk_with<fs::recursive_directory_iterator>(root, visit);
    }
    return walk_with<fs::directory_iterator>(root, visit);
}

struct file_stat
{
    fs::file_time_type last_mod_time{};
    std::uintmax_t size = 0;
    fs::file_type type = fs::file_type::none;

    auto operator==(const file_stat& rhs) const -> bool = default;
};

auto read_stat(const fs::directory_entry& entry) -> file_stat
{
    fs::error_code err;
    file_stat stat;
    stat.last_mod_time = entry.last_write_time(err);
    stat.size = entry.file_size(err);
    stat.type = entry.status(err).type();
    return stat;
}

auto get_stat(const watcher::entry& entry) -> file_stat
{
    return {entry.last_mod_time, entry.size, entry.type};
}

auto make_entry(const fs::path& path,
                const file_stat& stat,
                watcher::entry_status status,
                std::chrono::system_clock::time_point event_time) -> watcher::entry
{
    watcher::entry entry;
    entry.path = path;
    entry.last_path = path;
    entry.status = status;
    entry.last_mod_time = stat.last_mod_time;
    entry.size = stat.size;
    entry.type = stat.type;
    entry.event_time = event_time;
    return entry;
}

/// The rename rule for files: same size and extension chain, and not newer than the missing entry.
auto is_rename_of(const watcher::entry& created, const fs::path& old_path, const file_stat& old_stat) -> bool
{
    if(created.size != old_stat.size || created.type != old_stat.type)
    {
        return false;
    }
    const auto newer_by = std::chrono::duration_cast<std::chrono::milliseconds>(created.last_mod_time - old_stat.last_mod_time);
    return newer_by <= 0ms && has_same_extensions(created.path, old_path);
}

/**
 * @brief The cache of one watched tree and the diff of a scan against it.
 *
 * Every entry the fallback reports comes from here. A rename is a missing entry paired with a
 * created one, so both halves have to be in one diff. Two holds make that independent of when a
 * scan runs:
 *   - a missing entry with no partner stays a rename candidate for the hold time before it is
 *     reported removed (delete, then the new name appears);
 *   - a new file that matches a file still present - same size, modification time and extension
 *     chain, which is what a copy keeps - waits the hold time for the original to disappear
 *     (copy, then delete the original).
 * A missing entry that comes back while held was replaced, not removed: it is reported modified.
 */
class change_tracker
{
public:
    explicit change_tracker(clock_type::duration hold_time)
        : hold_time_(hold_time)
    {
    }

    /// Fills the cache from a walk without reporting anything.
    void reset(const fs::path& root, bool recursive)
    {
        const auto generation = ++generation_;
        walk_directory(root,
                       recursive,
                       [&](const fs::directory_entry& entry) -> void
                       {
                           auto& cached = entries_[entry.path().native()];
                           cached.path = entry.path();
                           cached.stat = read_stat(entry);
                           cached.seen_scan = generation;
                       });
    }

    struct scan_result
    {
        std::uint64_t generation = 0;
        bool is_complete = false;
        /// Created, modified, or unmodified for a held missing entry that came back.
        std::vector<watcher::entry> changes;
    };

    /// Walks the tree and records what differs from the cache, which stays as it is until commit().
    auto scan(const fs::path& root, bool recursive) -> scan_result
    {
        scan_result result;
        result.generation = ++generation_;
        result.is_complete = walk_directory(root,
                                            recursive,
                                            [&](const fs::directory_entry& entry) -> void
                                            {
                                                record(entry, result);
                                            });
        return result;
    }

    /// Applies a scan to the cache and returns what to report, in scan order, removals last.
    auto commit(scan_result& scan, time_point now) -> std::vector<watcher::entry>
    {
        std::vector<watcher::entry> reported;
        reported.reserve(scan.changes.size());
        std::vector<creation> creations;
        apply_changes(scan, reported, creations);
        auto missing = collect_missing(scan.generation, now);
        forget_vanished_holds(scan.generation);
        if(!missing.empty() && !creations.empty())
        {
            match_renames(reported, creations, missing);
        }
        std::vector<bool> is_held(reported.size(), false);
        settle_creations(reported, creations, is_held, scan.generation, now);
        auto removed = release_expired_removals(missing, now);
        update_hold_deadline();
        std::vector<watcher::entry> result;
        result.reserve(reported.size() + removed.size());
        for(std::size_t index = 0; index < reported.size(); ++index)
        {
            if(!is_held[index])
            {
                result.push_back(std::move(reported[index]));
            }
        }
        std::move(removed.begin(), removed.end(), std::back_inserter(result));
        return result;
    }

    /// When the earliest held entry is due to be reported.
    auto get_hold_deadline() const -> const hpp::optional<time_point>&
    {
        return hold_deadline_;
    }

private:
    struct cached_entry
    {
        fs::path path;
        file_stat stat;
        std::uint64_t seen_scan = 0;
        bool is_missing = false;
        time_point missing_since{};
    };

    struct held_creation
    {
        time_point since{};
        std::uint64_t seen_scan = 0;
    };

    /// A created entry of the scan being committed.
    struct creation
    {
        std::size_t index = 0;
        bool was_held = false;
        time_point held_since{};
    };

    /// Missing entries still free to pair with a created one.
    struct rename_candidates
    {
        std::unordered_set<path_key> unmatched;
        std::map<std::uintmax_t, std::vector<path_key>> files_by_size;
        std::vector<path_key> directories;
        /// Names of the missing entries in each missing folder.
        std::unordered_map<path_key, std::vector<path_key>> children;
    };

    void record(const fs::directory_entry& entry, scan_result& result)
    {
        const file_stat stat = read_stat(entry);
        const auto it = entries_.find(entry.path().native());
        if(it == entries_.end())
        {
            result.changes.push_back(make_entry(entry.path(), stat, watcher::entry_status::created, {}));
            return;
        }
        auto& cached = it->second;
        cached.seen_scan = result.generation;
        if(cached.is_missing)
        {
            result.changes.push_back(make_entry(entry.path(), stat, watcher::entry_status::unmodified, {}));
            return;
        }
        if(!(cached.stat == stat))
        {
            result.changes.push_back(make_entry(entry.path(), stat, watcher::entry_status::modified, {}));
        }
    }

    void apply_changes(scan_result& scan, std::vector<watcher::entry>& reported, std::vector<creation>& creations)
    {
        const auto now_system = std::chrono::system_clock::now();
        for(auto& change : scan.changes)
        {
            const auto& key = change.path.native();
            if(change.status == watcher::entry_status::created)
            {
                creation created;
                created.index = reported.size();
                const auto held = held_creations_.find(key);
                if(held != held_creations_.end())
                {
                    created.was_held = true;
                    created.held_since = held->second.since;
                    held->second.seen_scan = scan.generation;
                }
                change.event_time = now_system;
                reported.push_back(std::move(change));
                creations.push_back(created);
                continue;
            }
            auto& cached = entries_.at(key);
            const bool came_back = change.status == watcher::entry_status::unmodified;
            if(came_back)
            {
                cached.is_missing = false;
                if(cached.stat == get_stat(change))
                {
                    continue;
                }
            }
            cached.stat = get_stat(change);
            change.status = watcher::entry_status::modified;
            // A modification is timed by the file; a replacement is timed when it was seen.
            change.event_time = came_back ? now_system : fs::filetime_to_system_clock(change.last_mod_time);
            reported.push_back(std::move(change));
        }
    }

    auto collect_missing(std::uint64_t generation, time_point now) -> std::vector<path_key>
    {
        std::vector<path_key> missing;
        for(auto& [key, cached] : entries_)
        {
            if(cached.seen_scan == generation)
            {
                continue;
            }
            if(!cached.is_missing)
            {
                cached.is_missing = true;
                cached.missing_since = now;
            }
            missing.push_back(key);
        }
        std::sort(missing.begin(), missing.end());
        return missing;
    }

    void forget_vanished_holds(std::uint64_t generation)
    {
        for(auto it = held_creations_.begin(); it != held_creations_.end();)
        {
            it = it->second.seen_scan == generation ? std::next(it) : held_creations_.erase(it);
        }
    }

    auto make_candidates(const std::vector<path_key>& missing) const -> rename_candidates
    {
        rename_candidates candidates;
        candidates.unmatched.insert(missing.begin(), missing.end());
        for(const auto& key : missing)
        {
            const auto& cached = entries_.at(key);
            if(cached.stat.type == fs::file_type::directory)
            {
                candidates.directories.push_back(key);
            }
            else
            {
                candidates.files_by_size[cached.stat.size].push_back(key);
            }
            candidates.children[cached.path.parent_path().native()].push_back(cached.path.filename().native());
        }
        for(auto& [parent, names] : candidates.children)
        {
            std::sort(names.begin(), names.end());
        }
        return candidates;
    }

    static auto index_created_children(const std::vector<watcher::entry>& reported, const std::vector<creation>& creations)
        -> std::unordered_map<path_key, std::vector<path_key>>
    {
        std::unordered_map<path_key, std::vector<path_key>> children;
        for(const auto& created : creations)
        {
            const auto& path = reported[created.index].path;
            children[path.parent_path().native()].push_back(path.filename().native());
        }
        for(auto& [parent, names] : children)
        {
            std::sort(names.begin(), names.end());
        }
        return children;
    }

    void match_renames(std::vector<watcher::entry>& reported,
                       const std::vector<creation>& creations,
                       const std::vector<path_key>& missing)
    {
        auto candidates = make_candidates(missing);
        const auto created_children = index_created_children(reported, creations);
        const auto created_empty_directories = std::count_if(creations.begin(),
                                                             creations.end(),
                                                             [&](const creation& created) -> bool
                                                             {
                                                                 const auto& entry = reported[created.index];
                                                                 return entry.type == fs::file_type::directory &&
                                                                        created_children.count(entry.path.native()) == 0;
                                                             });
        std::vector<std::size_t> renamed_directories;
        for(const auto& created : creations)
        {
            auto& entry = reported[created.index];
            if(match_renamed_parent(entry, reported, renamed_directories, candidates))
            {
                continue;
            }
            const bool is_directory = entry.type == fs::file_type::directory;
            const auto old_key = is_directory
                                     ? find_renamed_directory(entry, candidates, created_children, created_empty_directories)
                                     : find_renamed_file(entry, candidates);
            if(!old_key)
            {
                continue;
            }
            mark_renamed(entry, *old_key, candidates);
            if(is_directory)
            {
                renamed_directories.push_back(created.index);
            }
        }
    }

    /// An entry under a renamed folder was renamed with it, when its old path is missing.
    auto match_renamed_parent(watcher::entry& entry,
                              const std::vector<watcher::entry>& reported,
                              const std::vector<std::size_t>& renamed_directories,
                              rename_candidates& candidates) -> bool
    {
        for(const auto index : renamed_directories)
        {
            const auto& directory = reported[index];
            if(!fs::is_any_parent_path(directory.path, entry.path))
            {
                continue;
            }
            const fs::path old_path = get_original_path(directory.last_path, directory.path, entry.path);
            if(candidates.unmatched.count(old_path.native()) == 0)
            {
                return false;
            }
            mark_renamed(entry, old_path.native(), candidates);
            return true;
        }
        return false;
    }

    auto find_renamed_file(const watcher::entry& entry, const rename_candidates& candidates) const -> hpp::optional<path_key>
    {
        const auto bucket = candidates.files_by_size.find(entry.size);
        if(bucket == candidates.files_by_size.end())
        {
            return hpp::nullopt;
        }
        for(const auto& key : bucket->second)
        {
            if(candidates.unmatched.count(key) == 0)
            {
                continue;
            }
            const auto& old = entries_.at(key);
            if(is_rename_of(entry, old.path, old.stat))
            {
                return key;
            }
        }
        return hpp::nullopt;
    }

    /**
     * @brief Pairs a new folder with a missing one.
     *
     * A folder's timestamp from a directory listing can be stale until the folder is renamed, so a
     * renamed folder may look newer than its cached self; its children move with it unchanged, so
     * the folders are compared by the names under them first. Empty folders pair only when exactly
     * one of each is in the diff; the timestamp rule of files is the last resort.
     */
    auto find_renamed_directory(const watcher::entry& entry,
                                const rename_candidates& candidates,
                                const std::unordered_map<path_key, std::vector<path_key>>& created_children,
                                std::ptrdiff_t created_empty_directories) const -> hpp::optional<path_key>
    {
        static const std::vector<path_key> no_children;
        const auto lookup = [](const auto& index, const path_key& parent) -> const std::vector<path_key>&
        {
            const auto it = index.find(parent);
            return it == index.end() ? no_children : it->second;
        };
        const auto& new_children = lookup(created_children, entry.path.native());
        std::vector<path_key> empty_matches;
        for(const auto& key : candidates.directories)
        {
            if(candidates.unmatched.count(key) == 0)
            {
                continue;
            }
            const auto& old = entries_.at(key);
            if(!has_same_extensions(entry.path, old.path))
            {
                continue;
            }
            const auto& old_children = lookup(candidates.children, key);
            if(!new_children.empty() && new_children == old_children)
            {
                return key;
            }
            if(new_children.empty() && old_children.empty())
            {
                empty_matches.push_back(key);
            }
        }
        if(empty_matches.size() == 1 && created_empty_directories == 1)
        {
            return empty_matches.front();
        }
        for(const auto& key : candidates.directories)
        {
            if(candidates.unmatched.count(key) == 0)
            {
                continue;
            }
            const auto& old = entries_.at(key);
            if(is_rename_of(entry, old.path, old.stat))
            {
                return key;
            }
        }
        return hpp::nullopt;
    }

    void mark_renamed(watcher::entry& entry, const path_key& old_key, rename_candidates& candidates)
    {
        const auto old = entries_.find(old_key);
        entry.status = watcher::entry_status::renamed;
        entry.last_path = old->second.path;
        entry.event_time = std::chrono::system_clock::now();
        entries_.erase(old);
        candidates.unmatched.erase(old_key);
    }

    void settle_creations(const std::vector<watcher::entry>& reported,
                          const std::vector<creation>& creations,
                          std::vector<bool>& is_held,
                          std::uint64_t generation,
                          time_point now)
    {
        std::unordered_map<std::uintmax_t, std::vector<const cached_entry*>> present_files;
        bool is_indexed = false;
        for(const auto& created : creations)
        {
            const auto& entry = reported[created.index];
            const auto& key = entry.path.native();
            const bool is_hold_over = created.was_held && now - created.held_since >= hold_time_;
            if(entry.status == watcher::entry_status::created && !is_hold_over && entry.type == fs::file_type::regular)
            {
                if(!is_indexed)
                {
                    index_present_files(present_files);
                    is_indexed = true;
                }
                if(has_present_original(entry, present_files))
                {
                    auto& held = held_creations_[key];
                    held.since = created.was_held ? created.held_since : now;
                    held.seen_scan = generation;
                    is_held[created.index] = true;
                    continue;
                }
            }
            auto& cached = entries_[key];
            cached.path = entry.path;
            cached.stat = get_stat(entry);
            cached.seen_scan = generation;
            cached.is_missing = false;
            held_creations_.erase(key);
        }
    }

    void index_present_files(std::unordered_map<std::uintmax_t, std::vector<const cached_entry*>>& present_files) const
    {
        for(const auto& [key, cached] : entries_)
        {
            if(!cached.is_missing && cached.stat.type == fs::file_type::regular)
            {
                present_files[cached.stat.size].push_back(&cached);
            }
        }
    }

    /// A file still present that this one could be a copy of: a copy keeps size and modification time.
    static auto has_present_original(const watcher::entry& entry,
                                     const std::unordered_map<std::uintmax_t, std::vector<const cached_entry*>>& present_files)
        -> bool
    {
        const auto bucket = present_files.find(entry.size);
        if(bucket == present_files.end())
        {
            return false;
        }
        return std::any_of(bucket->second.begin(),
                           bucket->second.end(),
                           [&entry](const cached_entry* original) -> bool
                           {
                               return original->stat.last_mod_time == entry.last_mod_time &&
                                      has_same_extensions(original->path, entry.path);
                           });
    }

    auto release_expired_removals(const std::vector<path_key>& missing, time_point now) -> std::vector<watcher::entry>
    {
        std::vector<watcher::entry> removed;
        const auto now_system = std::chrono::system_clock::now();
        for(const auto& key : missing)
        {
            const auto it = entries_.find(key);
            if(it == entries_.end() || !it->second.is_missing || now - it->second.missing_since < hold_time_)
            {
                continue;
            }
            removed.push_back(make_entry(it->second.path, it->second.stat, watcher::entry_status::removed, now_system));
            entries_.erase(it);
        }
        return removed;
    }

    void update_hold_deadline()
    {
        hold_deadline_ = hpp::nullopt;
        const auto consider = [this](time_point since) -> void
        {
            const auto due = since + hold_time_;
            hold_deadline_ = hold_deadline_ ? std::min(*hold_deadline_, due) : due;
        };
        for(const auto& [key, cached] : entries_)
        {
            if(cached.is_missing)
            {
                consider(cached.missing_since);
            }
        }
        for(const auto& [key, held] : held_creations_)
        {
            consider(held.since);
        }
    }

    clock_type::duration hold_time_;
    std::unordered_map<path_key, cached_entry> entries_;
    std::unordered_map<path_key, held_creation> held_creations_;
    std::uint64_t generation_ = 0;
    hpp::optional<time_point> hold_deadline_;
};

} // namespace

/**
 * @brief Scans one watched root and decides when.
 *
 * A scan runs SETTLE_DELAY after the OS last reported a change, at the latest one poll interval
 * after the first unscanned one, when a held entry is due, and otherwise once per poll interval.
 * A change reported while a scan runs leaves the listener waiting for another scan; the result is
 * still reported, as the holds of change_tracker pair a rename the scan saw only half of. That
 * matters because a listing refreshes stale folder timestamps, which NTFS reports as a change.
 * A walk that stopped on an error is thrown away and repeated once the tree is quiet, unless the
 * listener has waited a whole poll interval already.
 * Everything except the atomics is touched by the polling thread only, after construction.
 */
class watcher_fallback::directory_listener
{
public:
    directory_listener(const fs::path& root,
                       bool recursive,
                       clock_type::duration poll_interval,
                       std::function<void()> wake)
        : root_(root)
        , recursive_(recursive)
        , poll_interval_(poll_interval)
        , wake_(std::move(wake))
        , tracker_(poll_interval / HOLD_DIVISOR)
    {
        start_trigger();
        scanned_counter_ = change_counter_.load();
        last_scan_ = clock_type::now();
        tracker_.reset(root_, recursive_);
    }

    ~directory_listener()
    {
        trigger_.stop();
    }

    directory_listener(const directory_listener&) = delete;
    auto operator=(const directory_listener&) -> directory_listener& = delete;

    auto get_path() const -> const fs::path&
    {
        return root_;
    }

    auto is_recursive() const -> bool
    {
        return recursive_;
    }

    void pause()
    {
        is_paused_ = true;
    }

    void resume()
    {
        is_paused_ = false;
    }

    void request_immediate_scan()
    {
        is_scan_requested_ = true;
    }

    auto get_due_time(time_point now) -> time_point
    {
        if(is_scan_requested_.load())
        {
            return now;
        }
        time_point due = last_scan_ + poll_interval_;
        const bool is_notified = change_counter_.load() != scanned_counter_;
        if(is_notified || needs_retry_)
        {
            if(!waiting_since_)
            {
                waiting_since_ = now;
            }
            const auto last_change = time_point(clock_type::duration(last_change_ticks_.load()));
            const auto quiet_at = std::max(last_change, last_scan_) + SETTLE_DELAY;
            due = std::min(quiet_at, *waiting_since_ + poll_interval_);
        }
        const auto& hold_deadline = tracker_.get_hold_deadline();
        return hold_deadline ? std::min(due, *hold_deadline) : due;
    }

    /// Scans and returns what to report; empty while paused or when the scan was thrown away.
    auto poll(time_point now) -> std::vector<watcher::entry>
    {
        is_scan_requested_ = false;
        if(!trigger_.is_active())
        {
            start_trigger();
        }
        const auto counter_before = change_counter_.load();
        auto scan = tracker_.scan(root_, recursive_);
        const bool changed_during_scan = change_counter_.load() != counter_before;
        last_scan_ = now;
        const bool is_overdue = waiting_since_ && now - *waiting_since_ >= poll_interval_;
        if(!scan.is_complete && !is_overdue)
        {
            needs_retry_ = true;
            if(!waiting_since_)
            {
                waiting_since_ = now;
            }
            return release_or_buffer({});
        }
        scanned_counter_ = counter_before;
        needs_retry_ = false;
        waiting_since_ = changed_during_scan ? hpp::optional<time_point>(now) : hpp::nullopt;
        return release_or_buffer(tracker_.commit(scan, clock_type::now()));
    }

private:
    void start_trigger()
    {
        trigger_.start(root_,
                       recursive_,
                       [this]() -> void
                       {
                           last_change_ticks_ = clock_type::now().time_since_epoch().count();
                           ++change_counter_;
                           wake_();
                       });
    }

    /// Holds results back while paused and hands them out with the first result after.
    auto release_or_buffer(std::vector<watcher::entry> changes) -> std::vector<watcher::entry>
    {
        if(is_paused_.load())
        {
            std::move(changes.begin(), changes.end(), std::back_inserter(buffered_));
            return {};
        }
        if(buffered_.empty())
        {
            return changes;
        }
        std::vector<watcher::entry> released;
        released.swap(buffered_);
        std::move(changes.begin(), changes.end(), std::back_inserter(released));
        return released;
    }

    fs::path root_;
    bool recursive_ = false;
    clock_type::duration poll_interval_;
    std::function<void()> wake_;
    change_tracker tracker_;
    std::vector<watcher::entry> buffered_;

    time_point last_scan_{};
    std::uint64_t scanned_counter_ = 0;
    /// Since when a scan has been owed; a scan is never thrown away after a whole poll interval.
    hpp::optional<time_point> waiting_since_;
    bool needs_retry_ = false;

    std::atomic<bool> is_paused_{false};
    std::atomic<bool> is_scan_requested_{false};
    std::atomic<std::uint64_t> change_counter_{0};
    std::atomic<clock_type::rep> last_change_ticks_{0};

    /// Declared last so it stops first: its callback touches the members above.
    change_trigger trigger_;
};

/**
 * @brief One watch: filters what its listener reports and calls back.
 */
class watcher_fallback::impl
{
public:
    impl(const fs::path& path,
         const fs::path& watch_root,
         const pattern_filter& filter,
         bool recursive,
         bool initial_list,
         watcher::notify_callback callback,
         std::shared_ptr<directory_listener> listener,
         const std::string& watcher_name)
        : path_(path)
        , watch_root_(watch_root)
        , filter_(filter)
        , recursive_(recursive)
        , callback_(std::move(callback))
        , listener_(std::move(listener))
        , is_listener_root_(watch_root_ == listener_->get_path())
        , init_time_timestamp_(std::chrono::system_clock::now())
        , watcher_name_(watcher_name)
    {
        if(initial_list)
        {
            emit_initial_list();
        }
    }

    impl(const impl&) = delete;
    auto operator=(const impl&) -> impl& = delete;

    auto get_listener() const -> const std::shared_ptr<directory_listener>&
    {
        return listener_;
    }

    void deliver(const std::vector<watcher::entry>& changes)
    {
        std::lock_guard<std::recursive_mutex> lock(callback_mutex_);
        if(!is_active_)
        {
            return;
        }
        std::vector<watcher::entry> filtered;
        for(const auto& entry : changes)
        {
            if(!filter_.should_include(entry.path) || !is_path_under_watch(entry.path))
            {
                continue;
            }
            if(entry.event_time < init_time_timestamp_)
            {
                continue;
            }
            filtered.push_back(entry);
        }
        if(!filtered.empty())
        {
            callback_(filtered, false);
        }
    }

    /// No callback starts after this returns; waits for a running one unless called from inside it.
    void deactivate()
    {
        std::lock_guard<std::recursive_mutex> lock(callback_mutex_);
        is_active_ = false;
    }

private:
    void emit_initial_list()
    {
        std::vector<watcher::entry> initial_entries;
        walk_directory(path_,
                       recursive_,
                       [&](const fs::directory_entry& entry) -> void
                       {
                           if(!filter_.should_include(entry.path()))
                           {
                               return;
                           }
                           auto initial = make_entry(entry.path(),
                                                     read_stat(entry),
                                                     watcher::entry_status::created,
                                                     std::chrono::system_clock::now());
                           initial_entries.push_back(std::move(initial));
                       });
        if(!initial_entries.empty())
        {
            callback_(initial_entries, true);
        }
    }

    /// Lexical: entries are spelled from the listener root, which is this root or an ancestor of it.
    auto is_path_under_watch(const fs::path& event_path) const -> bool
    {
        return is_listener_root_ || event_path == watch_root_ || fs::is_any_parent_path(watch_root_, event_path);
    }

    fs::path path_;
    fs::path watch_root_;
    pattern_filter filter_;
    bool recursive_ = false;
    watcher::notify_callback callback_;
    std::shared_ptr<directory_listener> listener_;
    bool is_listener_root_ = false;
    std::chrono::system_clock::time_point init_time_timestamp_;
    std::string watcher_name_;

    std::recursive_mutex callback_mutex_;
    bool is_active_ = true;
};

watcher_fallback::~watcher_fallback()
{
    close();
}

void watcher_fallback::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(pause_depth_++ > 0)
    {
        return;
    }
    for(const auto& listener : directory_listeners_)
    {
        listener->pause();
    }
}

void watcher_fallback::resume()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(pause_depth_ == 0 || --pause_depth_ > 0)
        {
            return;
        }
        for(const auto& listener : directory_listeners_)
        {
            listener->resume();
            listener->request_immediate_scan();
        }
    }
    wake();
}

void watcher_fallback::wait_all(watcher::clock_t::duration duration)
{
    wake();
    std::this_thread::sleep_for(duration);
}

void watcher_fallback::close()
{
    watching_ = false;
    unwatch_all_impl();
    wake();
    if(thread_.joinable())
    {
        thread_.join();
    }
}

void watcher_fallback::start()
{
    bool expected = false;
    if(!watching_.compare_exchange_strong(expected, true))
    {
        return;
    }
    thread_ = std::thread(
        [this]() -> void
        {
            platform::set_thread_name("fs::watcher");
            run();
        });
}

void watcher_fallback::run()
{
    while(watching_)
    {
        std::vector<std::shared_ptr<directory_listener>> listeners;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(pause_depth_ == 0)
            {
                listeners = directory_listeners_;
            }
        }
        if(listeners.empty())
        {
            // Paused, or nothing to watch: watch(), resume() and close() wake the thread.
            wait_for_wake(clock_type::now() + PAUSED_WAIT);
            continue;
        }
        auto wake_at = clock_type::now() + MAX_WAIT;
        for(const auto& listener : listeners)
        {
            const auto now = clock_type::now();
            if(listener->get_due_time(now) <= now)
            {
                const auto changes = listener->poll(now);
                if(!changes.empty())
                {
                    deliver(listener, changes);
                }
            }
            wake_at = std::min(wake_at, listener->get_due_time(clock_type::now()));
        }
        wait_for_wake(wake_at);
    }
}

void watcher_fallback::wake()
{
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        wake_pending_ = true;
    }
    wake_cv_.notify_one();
}

void watcher_fallback::wait_for_wake(watcher::clock_t::time_point deadline)
{
    std::unique_lock<std::mutex> lock(wake_mutex_);
    wake_cv_.wait_until(lock,
                        deadline,
                        [this]() -> bool
                        {
                            return wake_pending_ || !watching_;
                        });
    wake_pending_ = false;
}

void watcher_fallback::deliver(const std::shared_ptr<directory_listener>& listener,
                               const std::vector<watcher::entry>& changes)
{
    std::vector<std::shared_ptr<impl>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& [key, watch] : watchers_)
        {
            if(watch->get_listener() == listener)
            {
                targets.push_back(watch);
            }
        }
    }
    for(const auto& target : targets)
    {
        target->deliver(changes);
    }
}

auto watcher_fallback::find_listener(const fs::path& root, bool recursive) const -> std::shared_ptr<directory_listener>
{
    for(const auto& listener : directory_listeners_)
    {
        if(listener->get_path() == root && (listener->is_recursive() || !recursive))
        {
            return listener;
        }
    }
    for(const auto& listener : directory_listeners_)
    {
        if(listener->is_recursive() && fs::is_any_parent_path(listener->get_path(), root))
        {
            return listener;
        }
    }
    return nullptr;
}

auto watcher_fallback::take_stale_listeners() -> std::vector<std::shared_ptr<directory_listener>>
{
    std::vector<std::shared_ptr<directory_listener>> stale;
    const auto is_unused = [this](const std::shared_ptr<directory_listener>& listener) -> bool
    {
        return std::none_of(watchers_.begin(),
                            watchers_.end(),
                            [&listener](const auto& kvp) -> bool
                            {
                                return kvp.second->get_listener() == listener;
                            });
    };
    const auto first_stale = std::stable_partition(directory_listeners_.begin(),
                                                   directory_listeners_.end(),
                                                   [&](const auto& listener) -> bool
                                                   {
                                                       return !is_unused(listener);
                                                   });
    std::move(first_stale, directory_listeners_.end(), std::back_inserter(stale));
    directory_listeners_.erase(first_stale, directory_listeners_.end());
    return stale;
}

auto watcher_fallback::watch_impl(const fs::path& path,
                                  const pattern_filter& filter,
                                  bool recursive,
                                  bool initial_list,
                                  watcher::clock_t::duration poll_interval,
                                  watcher::notify_callback callback,
                                  const std::string& watcher_name) -> std::uint64_t
{
    if(!callback)
    {
        return 0;
    }
    const fs::path root = make_absolute(path);
    std::shared_ptr<directory_listener> listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listener = find_listener(root, recursive);
    }
    const bool is_new_listener = !listener;
    if(is_new_listener)
    {
        // Built outside the lock: it walks the whole tree.
        listener = std::make_shared<directory_listener>(root,
                                                        recursive,
                                                        poll_interval,
                                                        [this]() -> void
                                                        {
                                                            wake();
                                                        });
    }
    static std::atomic<std::uint64_t> free_id = {1};
    const auto key = free_id++;
    auto watch =
        std::make_shared<impl>(path, root, filter, recursive, initial_list, std::move(callback), listener, watcher_name);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(is_new_listener)
        {
            if(pause_depth_ > 0)
            {
                listener->pause();
            }
            directory_listeners_.push_back(listener);
        }
        watchers_[key] = std::move(watch);
    }
    wake();
    return key;
}

void watcher_fallback::unwatch_impl(std::uint64_t key)
{
    std::shared_ptr<impl> removed;
    std::vector<std::shared_ptr<directory_listener>> stale;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = watchers_.find(key);
        if(it != watchers_.end())
        {
            removed = std::move(it->second);
            watchers_.erase(it);
        }
        stale = take_stale_listeners();
    }
    if(removed)
    {
        removed->deactivate();
    }
    wake();
}

void watcher_fallback::unwatch_all_impl()
{
    std::map<std::uint64_t, std::shared_ptr<impl>> removed;
    std::vector<std::shared_ptr<directory_listener>> listeners;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removed.swap(watchers_);
        listeners.swap(directory_listeners_);
    }
    for(const auto& [key, watch] : removed)
    {
        watch->deactivate();
    }
    wake();
}

} // namespace fs
