#include "mirror.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs
{
namespace
{

/// The key two names of one file compare equal under: case-insensitive on Windows.
auto make_name_key(const path& relative_path) -> std::string
{
    auto key = relative_path.generic_string();
#ifdef _WIN32
    std::transform(key.begin(),
                   key.end(),
                   key.begin(),
                   [](char c)
                   {
                       return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
                   });
#endif
    return key;
}

auto has_same_size_and_time(const path& from, const path& to) -> bool
{
    error_code ec;
    if(!is_regular_file(to, ec))
    {
        return false;
    }
    const auto from_size = file_size(from, ec);
    const auto to_size = ec ? 0 : file_size(to, ec);
    if(ec || from_size != to_size)
    {
        return false;
    }
    const auto from_time = last_write_time(from, ec);
    const auto to_time = ec ? file_time_type{} : last_write_time(to, ec);
    return !ec && from_time == to_time;
}

/// Removes the files under `to` whose names `kept` does not hold, then the folders left empty.
void remove_unmirrored(const path& to, const std::unordered_set<std::string>& kept, mirror_stats& stats)
{
    error_code ec;
    std::vector<path> stale_files;
    std::vector<path> directories;
    for(recursive_directory_iterator it(to, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            directories.push_back(it->path());
        }
        else if(!kept.contains(make_name_key(relative(it->path(), to, ec))))
        {
            stale_files.push_back(it->path());
        }
    }
    for(const auto& file : stale_files)
    {
        if(remove(file, ec))
        {
            ++stats.removed;
        }
    }
    // Deepest first, so a folder emptied by removing its subfolders goes too.
    std::sort(directories.begin(),
              directories.end(),
              [](const path& lhs, const path& rhs)
              {
                  return lhs.native().size() > rhs.native().size();
              });
    for(const auto& directory : directories)
    {
        if(is_empty(directory, ec))
        {
            remove(directory, ec);
        }
    }
}

} // namespace

auto copy_file_if_changed(const path& from, const path& to, bool& is_copied, error_code& ec) -> bool
{
    is_copied = false;
    ec.clear();
    if(has_same_size_and_time(from, to))
    {
        return true;
    }
    create_directories(to.parent_path(), ec);
    if(ec || !copy_file(from, to, copy_options::overwrite_existing, ec))
    {
        return false;
    }
    const auto time = last_write_time(from, ec);
    if(ec)
    {
        return false;
    }
    last_write_time(to, time, ec);
    is_copied = !ec;
    return is_copied;
}

auto mirror_directory(const path& from,
                      const path& to,
                      const mirror_filter& include,
                      mirror_stats& stats,
                      error_code& ec) -> bool
{
    stats = {};
    ec.clear();
    std::unordered_set<std::string> kept;
    for(recursive_directory_iterator it(from, ec), end; it != end; it.increment(ec))
    {
        if(ec)
        {
            return false;
        }
        if(!it->is_regular_file(ec) || (include && !include(it->path())))
        {
            continue;
        }
        const auto relative_path = relative(it->path(), from, ec);
        bool is_copied = false;
        if(ec || !copy_file_if_changed(it->path(), to / relative_path, is_copied, ec))
        {
            return false;
        }
        ++(is_copied ? stats.copied : stats.unchanged);
        kept.insert(make_name_key(relative_path));
    }
    if(ec)
    {
        return false;
    }
    remove_unmirrored(to, kept, stats);
    return true;
}

} // namespace fs
