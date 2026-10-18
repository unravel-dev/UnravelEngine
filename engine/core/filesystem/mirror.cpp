#include "mirror.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs
{
namespace
{
/// The suffix of a copy on its way into place.
constexpr const char* MIRROR_TEMP_SUFFIX = ".mirror-tmp";

auto make_relative_key(const path& relative_path) -> std::string
{
    return make_path_key(relative_path.generic_string());
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

/// Lists the files under `to` whose names `kept` does not hold, and every folder.
auto find_unmirrored(const path& to,
                     const std::unordered_set<std::string>& kept,
                     std::vector<path>& stale_files,
                     std::vector<path>& directories,
                     error_code& ec) -> bool
{
    for(recursive_directory_iterator it(to, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            directories.push_back(it->path());
            continue;
        }
        const auto relative_path = it->path().lexically_relative(to);
        if(!ec && !kept.contains(make_relative_key(relative_path)))
        {
            stale_files.push_back(it->path());
        }
    }
    return !ec;
}

/// Removes the files under `to` whose names `kept` does not hold, then the folders left empty.
auto remove_unmirrored(const path& to, const std::unordered_set<std::string>& kept, mirror_stats& stats, error_code& ec)
    -> bool
{
    std::vector<path> stale_files;
    std::vector<path> directories;
    if(!find_unmirrored(to, kept, stale_files, directories, ec))
    {
        return false;
    }
    for(const auto& file : stale_files)
    {
        if(!remove(file, ec))
        {
            return false;
        }
        ++stats.removed;
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
        error_code empty_ec;
        if(is_empty(directory, empty_ec) && !remove(directory, ec))
        {
            return false;
        }
    }
    return true;
}

/// Copies `from` to a temporary file beside `to` with the source's write time, then renames it into
/// place: a file in use (a running game's library or pack) is replaced, never rewritten under it.
auto copy_through_temp(const path& from, const path& to, error_code& ec) -> bool
{
    path temp = to;
    temp += MIRROR_TEMP_SUFFIX;
    const bool is_copied = copy_file(from, temp, copy_options::overwrite_existing, ec);
    const auto time = is_copied ? last_write_time(from, ec) : file_time_type{};
    if(is_copied && !ec)
    {
        last_write_time(temp, time, ec);
    }
    if(is_copied && !ec)
    {
        rename(temp, to, ec);
    }
    if(ec)
    {
        error_code cleanup_ec;
        remove(temp, cleanup_ec);
        return false;
    }
    return true;
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
    if(ec || !copy_through_temp(from, to, ec))
    {
        return false;
    }
    is_copied = true;
    return true;
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
    for(recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec))
    {
        if(!it->is_regular_file(ec) || (include && !include(it->path())))
        {
            continue;
        }
        const auto relative_path = it->path().lexically_relative(from);
        bool is_copied = false;
        if(!copy_file_if_changed(it->path(), to / relative_path, is_copied, ec))
        {
            return false;
        }
        ++(is_copied ? stats.copied : stats.unchanged);
        kept.insert(make_relative_key(relative_path));
    }
    return !ec && remove_unmirrored(to, kept, stats, ec);
}

} // namespace fs
