#include "package_library.h"
#include "package_archive.h"
#include "package_version.h"

#include <algorithm>
#include <unordered_set>

namespace unravel::packages
{
namespace
{
auto is_package_file(const fs::directory_entry& entry) -> bool
{
    fs::error_code ec;
    return entry.is_regular_file(ec) && fs::make_path_key(entry.path().extension().string()) == fs::make_path_key(PACKAGE_EXTENSION);
}

auto compare_versions(const std::string& lhs, const std::string& rhs) -> bool
{
    const auto left = parse_version(lhs);
    const auto right = parse_version(rhs);
    if(left && right)
    {
        return *left > *right;
    }
    return lhs > rhs;
}
} // namespace

auto get_packages_folder(const fs::path& project_root) -> fs::path
{
    return project_root / PACKAGES_FOLDER_NAME;
}

auto scan_library(const std::vector<fs::path>& folders) -> std::vector<library_entry>
{
    std::vector<library_entry> entries;
    std::unordered_set<std::string> seen;
    for(const auto& folder : folders)
    {
        fs::error_code ec;
        for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        {
            if(!is_package_file(*it) || !seen.insert(fs::make_path_key(it->path().generic_string())).second)
            {
                continue;
            }
            library_entry entry;
            entry.file = it->path();
            package_archive archive;
            if(archive.open(entry.file, entry.error))
            {
                entry.manifest = archive.get_manifest();
                entry.size = archive.get_file_size();
            }
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(),
              entries.end(),
              [](const library_entry& lhs, const library_entry& rhs)
              {
                  const auto& left_title = lhs.manifest.get_title();
                  const auto& right_title = rhs.manifest.get_title();
                  if(left_title != right_title)
                  {
                      return fs::make_path_key(left_title) < fs::make_path_key(right_title);
                  }
                  return compare_versions(lhs.manifest.version, rhs.manifest.version);
              });
    return entries;
}

auto find_newest(const std::vector<library_entry>& entries, const std::string& name) -> const library_entry*
{
    const library_entry* newest = nullptr;
    for(const auto& entry : entries)
    {
        if(!entry.error.empty() || entry.manifest.name != name)
        {
            continue;
        }
        if(newest == nullptr || compare_versions(entry.manifest.version, newest->manifest.version))
        {
            newest = &entry;
        }
    }
    return newest;
}

} // namespace unravel::packages
