#include "mirror.h"
#include "shared_file.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs
{
namespace
{
/// The suffix of a copy on its way into place.
constexpr const char* MIRROR_TEMP_SUFFIX = ".mirror-tmp";
/// How much of two files is read at a time to compare them.
constexpr std::size_t COMPARE_BLOCK_SIZE = 64 * 1024;

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
    // A source with no file to mirror never creates `to`, and a folder that is not there holds nothing stale.
    if(!exists(to, ec))
    {
        return !ec;
    }
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
/// place: a file in use is replaced where its user allows that (a site's file a server is sending), never
/// rewritten under it.
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
        rename_over(temp, to, ec);
    }
    if(ec)
    {
        error_code cleanup_ec;
        remove(temp, cleanup_ec);
        return false;
    }
    return true;
}

/// Whether a '/' separated relative name stays under the folder it is joined to: not absolute, and no "..".
auto is_name_inside(const std::string& name) -> bool
{
    const path relative = from_utf8_string(name);
    return !name.empty() && relative.is_relative() && !relative.has_root_name() &&
           std::none_of(relative.begin(),
                        relative.end(),
                        [](const path& part)
                        {
                            return part == "..";
                        });
}

/// Removes the folders under `root` that the relative path `file` lies in, deepest first, while they are empty.
void remove_empty_folders_of(const path& root, const path& file)
{
    error_code ec;
    auto folder = file.parent_path();
    while(!folder.empty() && remove(root / folder, ec))
    {
        folder = folder.parent_path();
    }
}

/// Whether the two streams hold the same bytes up to their ends.
auto have_same_bytes(std::ifstream& lhs, std::ifstream& rhs) -> bool
{
    std::vector<char> lhs_block(COMPARE_BLOCK_SIZE);
    std::vector<char> rhs_block(COMPARE_BLOCK_SIZE);
    while(lhs && rhs)
    {
        lhs.read(lhs_block.data(), static_cast<std::streamsize>(lhs_block.size()));
        rhs.read(rhs_block.data(), static_cast<std::streamsize>(rhs_block.size()));
        const auto read = lhs.gcount();
        if(read != rhs.gcount() || !std::equal(lhs_block.begin(), lhs_block.begin() + read, rhs_block.begin()))
        {
            return false;
        }
    }
    return lhs.eof() && rhs.eof();
}
} // namespace

auto have_same_content(const path& lhs, const path& rhs, error_code& ec) -> bool
{
    const auto lhs_size = file_size(lhs, ec);
    const auto rhs_size = ec ? 0 : file_size(rhs, ec);
    if(ec || lhs_size != rhs_size)
    {
        return false;
    }
    std::ifstream lhs_stream(lhs, std::ios::binary);
    std::ifstream rhs_stream(rhs, std::ios::binary);
    if(!lhs_stream || !rhs_stream)
    {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }
    return have_same_bytes(lhs_stream, rhs_stream);
}

auto remove_unlisted_files(const path& root,
                           const std::vector<std::string>& earlier,
                           const std::vector<std::string>& kept,
                           error_code& ec) -> std::size_t
{
    ec.clear();
    std::unordered_set<std::string> kept_keys;
    for(const auto& name : kept)
    {
        kept_keys.insert(make_path_key(name));
    }
    std::size_t removed = 0;
    for(const auto& name : earlier)
    {
        if(kept_keys.contains(make_path_key(name)) || !is_name_inside(name))
        {
            continue;
        }
        const auto relative = from_utf8_string(name);
        error_code file_ec;
        if(remove(root / relative, file_ec))
        {
            ++removed;
            remove_empty_folders_of(root, relative);
        }
        if(file_ec && !ec)
        {
            ec = file_ec;
        }
    }
    return removed;
}

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
