#include "folders.h"
#include "files.h"
#include "releases.h"

#include <engine/assets/impl/asset_extensions.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>

namespace unravel::deployment
{
namespace
{
// The project folders the project manager creates for its settings, and the scripts' build folder
// (the project's .csproj builds into temp/bin and temp/obj).
constexpr const char* PROJECT_SETTINGS_FOLDER = "settings";
constexpr const char* PROJECT_DEPLOY_SETTINGS_FOLDER = "deploy";
constexpr const char* PROJECT_EDITOR_FOLDER = "editor";
constexpr const char* SCRIPT_BUILD_FOLDER = "temp";
} // namespace

auto is_previous_deploy(const fs::path& folder) -> bool
{
    fs::error_code ec;
    const auto app_root = folder / DATA_FOLDER / "app";
    return fs::exists(app_root / ex::get_asset_index_file_name(), ec) || fs::exists(app_root / ex::get_pack_file_name(), ec) ||
           fs::exists(app_root / LEGACY_ASSET_INDEX_NAME, ec);
}

auto is_android_package_folder(const fs::path& folder) -> bool
{
    fs::error_code ec;
    bool has_package = false;
    bool has_earlier_data = false;
    for(const auto& entry : fs::directory_iterator(folder, ec))
    {
        const auto& path = entry.path();
        if(entry.is_regular_file(ec) && string_utils::to_lower(path.extension().generic_string()) == ".apk")
        {
            has_package = true;
        }
        else if(entry.is_directory(ec) && path.filename() == DATA_FOLDER && is_previous_deploy(folder))
        {
            has_earlier_data = true;
        }
        else
        {
            return false;
        }
    }
    // data/ alone is a desktop build without its player, not an Android deploy's.
    return !ec && (has_package || !has_earlier_data);
}

auto is_inside(const fs::path& path, const fs::path& folder) -> bool
{
    fs::error_code ec;
    const auto canonical_folder = fs::weakly_canonical(folder, ec);
    const auto canonical_path = fs::weakly_canonical(path, ec);
    if(canonical_folder.empty() || canonical_path.empty())
    {
        return false;
    }
    const auto folder_key = fs::make_path_key(canonical_folder.generic_string());
    const auto path_key = fs::make_path_key(canonical_path.generic_string());
    return path_key.compare(0, folder_key.size(), folder_key) == 0 &&
           (path_key.size() == folder_key.size() || path_key[folder_key.size()] == '/' || folder_key.back() == '/');
}

auto get_managed_project_folder(const fs::path& path, const fs::path& project_root) -> std::string
{
    const auto relative = fs::make_relative_if_inside(path, project_root);
    if(relative.empty() || relative.has_root_path() || relative == ".")
    {
        return {};
    }
    const std::string managed[] = {ex::get_data_directory_no_slash(),
                                   ex::get_compiled_directory_no_slash(),
                                   PROJECT_SETTINGS_FOLDER,
                                   PROJECT_DEPLOY_SETTINGS_FOLDER,
                                   PROJECT_EDITOR_FOLDER,
                                   RELEASES_FOLDER,
                                   SCRIPT_BUILD_FOLDER};
    const auto top = fs::make_path_key(relative.begin()->generic_string());
    const auto found = std::find_if(std::begin(managed),
                                    std::end(managed),
                                    [&top](const std::string& folder)
                                    {
                                        return fs::make_path_key(folder) == top;
                                    });
    return found == std::end(managed) ? std::string{} : *found;
}

auto is_executable_in_use(const fs::path& executable) -> bool
{
#ifdef _WIN32
    fs::error_code ec;
    if(!fs::exists(executable, ec))
    {
        return false;
    }
    // Opening for writing, without truncating, fails while the loader holds the image.
    std::fstream probe(executable, std::ios::in | std::ios::out | std::ios::binary);
    return !probe.is_open();
#else
    (void)executable;
    return false;
#endif
}

} // namespace unravel::deployment
