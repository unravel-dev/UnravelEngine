#include "folders.h"
#include "files.h"
#include "profile.h"
#include "profile_cache.h"
#include "releases.h"

#include <engine/assets/impl/asset_extensions.h>
#include <hpp/string_view.hpp>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>
#include <iterator>

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
// A build stamp's keys, and its state values.
constexpr const char* STAMP_STATE_KEY = "state";
constexpr const char* STAMP_PROFILE_KEY = "profile";
constexpr const char* STAMP_ENGINE_VERSION_KEY = "engine_version";
constexpr const char* STAMP_PLATFORM_KEY = "platform";
constexpr const char* STAMP_FINISHED = "finished";
constexpr const char* STAMP_UNFINISHED = "unfinished";

auto make_stamp_line(const char* key, const std::string& value) -> std::string
{
    return std::string(key) + "=" + value + "\n";
}
} // namespace

auto write_build_stamp(const fs::path& folder, const build_stamp& stamp) -> bool
{
    const auto text = make_stamp_line(STAMP_STATE_KEY, stamp.is_finished ? STAMP_FINISHED : STAMP_UNFINISHED) +
                      make_stamp_line(STAMP_PROFILE_KEY, stamp.profile) +
                      make_stamp_line(STAMP_ENGINE_VERSION_KEY, stamp.engine_version) +
                      make_stamp_line(STAMP_PLATFORM_KEY, stamp.platform);
    return write_file(folder / BUILD_STAMP_NAME, text.data(), text.size());
}

auto read_build_stamp(const fs::path& folder) -> build_stamp
{
    auto values = read_stamp(folder / BUILD_STAMP_NAME);
    build_stamp stamp;
    stamp.profile = values[STAMP_PROFILE_KEY];
    stamp.engine_version = values[STAMP_ENGINE_VERSION_KEY];
    stamp.is_finished = values[STAMP_STATE_KEY] == STAMP_FINISHED;
    stamp.platform = values[STAMP_PLATFORM_KEY];
    return stamp;
}

auto is_finished_build(const fs::path& folder) -> bool
{
    return read_build_stamp(folder).is_finished;
}

auto get_other_platform_problem(const fs::path& folder, deploy_platform platform) -> std::string
{
    const auto built_for = read_build_stamp(folder).platform;
    if(built_for.empty() || built_for == get_platform_name(platform))
    {
        return {};
    }
    return fmt::format("The {} folder in the deploy folder holds a build for {}, whose files would ship with this one. "
                       "Empty it, or choose another deploy folder.",
                       folder.filename().generic_string(),
                       built_for);
}

auto is_previous_deploy(const fs::path& folder) -> bool
{
    const auto app_root = folder / DATA_FOLDER / "app";
    return is_present(folder / BUILD_STAMP_NAME) || is_present(app_root / ex::get_asset_index_file_name()) ||
           is_present(app_root / ex::get_pack_file_name()) || is_present(app_root / LEGACY_ASSET_INDEX_NAME);
}

auto is_android_package_folder(const fs::path& folder) -> bool
{
    fs::error_code ec;
    bool has_package = false;
    bool has_earlier_data = false;
    for(const auto& entry : fs::directory_iterator(folder, ec))
    {
        const auto& path = entry.path();
        const auto extension = string_utils::to_lower(path.extension().generic_string());
        if(entry.is_regular_file(ec) && (extension == ".apk" || extension == ".aab"))
        {
            has_package = true;
        }
        else if(entry.is_regular_file(ec) && path.filename() == BUILD_STAMP_NAME)
        {
            continue;
        }
        else if(const auto name = path.filename().generic_string();
                entry.is_regular_file(ec) && hpp::string_view(name).ends_with(hpp::string_view(NATIVE_SYMBOLS_FILE_SUFFIX)))
        {
            continue;
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
                                   PROFILES_FOLDER,
                                   CACHE_FOLDER,
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

auto is_same_folder_name(const std::string& lhs, const std::string& rhs) -> bool
{
    return fs::make_path_key(lhs) == fs::make_path_key(rhs);
}

auto get_executable_name_problem(const fs::path& executable) -> std::string
{
    const auto name = executable.filename().generic_string();
    if(!is_same_folder_name(name, DATA_FOLDER))
    {
        return {};
    }
    return fmt::format("The game's executable would be named {}, as the folder its data goes to. Rename the project to "
                       "deploy it.",
                       name);
}

auto read_whole_file(const fs::path& file, std::string& out) -> bool
{
    out.clear();
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

void rename_folder(const fs::path& from, const fs::path& to, fs::error_code& ec)
{
    ec.clear();
    if(fs::make_path_key(from.generic_string()) != fs::make_path_key(to.generic_string()))
    {
        fs::rename(from, to, ec);
        return;
    }
    auto through = to;
    through += ".renaming";
    fs::rename(from, through, ec);
    if(ec)
    {
        return;
    }
    fs::rename(through, to, ec);
    if(ec)
    {
        fs::error_code undo_ec;
        fs::rename(through, from, undo_ec);
    }
}

auto is_executable_in_use(const fs::path& executable) -> bool
{
#ifdef _WIN32
    if(!is_present(executable))
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
