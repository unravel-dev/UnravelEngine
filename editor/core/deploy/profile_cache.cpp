#include "profile_cache.h"
#include "folders.h"
#include "releases.h"

#include <logging/logging.h>

#include <algorithm>

namespace unravel::deployment
{
namespace
{
constexpr const char* PROFILE_CACHES_FOLDER = "profiles";

auto get_profile_caches_folder(const fs::path& project_root) -> fs::path
{
    return project_root / CACHE_FOLDER / PROFILE_CACHES_FOLDER;
}

/// Removes the folders under `folder` that hold nothing, deepest first.
void remove_empty_folders(const fs::path& folder)
{
    std::vector<fs::path> folders;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            folders.push_back(it->path());
        }
    }
    std::sort(folders.begin(),
              folders.end(),
              [](const fs::path& lhs, const fs::path& rhs)
              {
                  return lhs.native().size() > rhs.native().size();
              });
    for(const auto& empty : folders)
    {
        if(fs::is_empty(empty, ec))
        {
            fs::remove(empty, ec);
        }
    }
}
} // namespace

auto get_profile_cache_folder(const fs::path& project_root, const std::string& profile) -> fs::path
{
    return get_profile_caches_folder(project_root) / fs::from_utf8_string(profile);
}

auto get_profile_variants_folder(const fs::path& project_root, const std::string& profile, const std::string& protocol) -> fs::path
{
    return get_profile_cache_folder(project_root, profile) / protocol;
}

auto get_profile_symbols_folder(const fs::path& project_root, const std::string& profile) -> fs::path
{
    return get_profile_cache_folder(project_root, profile) / SYMBOLS_FOLDER;
}

auto sweep_folder(const fs::path& folder, const std::unordered_set<std::string>& kept) -> sweep_report
{
    std::vector<fs::path> stale;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            continue;
        }
        const auto relative = fs::make_path_key(it->path().lexically_relative(folder).generic_string());
        if(!kept.contains(relative))
        {
            stale.push_back(it->path());
        }
    }
    sweep_report report;
    for(const auto& file : stale)
    {
        if(fs::remove(file, ec))
        {
            ++report.removed;
        }
        else
        {
            report.failed.push_back(file);
        }
    }
    remove_empty_folders(folder);
    return report;
}

auto remove_profile_cache(const fs::path& project_root, const std::string& profile) -> result
{
    fs::error_code ec;
    fs::remove_all(get_profile_cache_folder(project_root, profile), ec);
    return ec ? result::failure(fmt::format("Deleting the texture cache of profile {} failed: {}", profile, ec.message()))
              : result{};
}

auto rename_profile_cache(const fs::path& project_root, const std::string& profile, const std::string& new_name) -> result
{
    const auto from = get_profile_cache_folder(project_root, profile);
    const auto to = get_profile_cache_folder(project_root, new_name);
    fs::error_code ec;
    if(!fs::exists(from, ec))
    {
        return {};
    }
    // A cache left under the new name belongs to no profile; it would only mix with this one.
    if(fs::make_path_key(profile) != fs::make_path_key(new_name))
    {
        fs::remove_all(to, ec);
    }
    rename_folder(from, to, ec);
    if(ec)
    {
        // The cache is derived data: without it the next deploy compiles it again.
        fs::error_code cleanup_ec;
        fs::remove_all(from, cleanup_ec);
    }
    return {};
}

auto remove_unknown_profile_caches(const fs::path& project_root, const std::vector<std::string>& profiles) -> std::vector<std::string>
{
    std::unordered_set<std::string> known;
    for(const auto& profile : profiles)
    {
        known.insert(fs::make_path_key(profile));
    }
    std::vector<std::string> removed;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(get_profile_caches_folder(project_root), ec))
    {
        const auto name = fs::to_utf8_string(entry.path().filename());
        if(known.contains(fs::make_path_key(name)))
        {
            continue;
        }
        fs::error_code remove_ec;
        fs::remove_all(entry.path(), remove_ec);
        if(!remove_ec)
        {
            removed.push_back(name);
        }
    }
    if(!removed.empty())
    {
        APPLOG_INFO("Deleted the texture caches of {} profile(s) the project no longer has", removed.size());
    }
    return removed;
}

} // namespace unravel::deployment
