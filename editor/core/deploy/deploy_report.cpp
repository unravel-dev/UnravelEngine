#include "deploy_report.h"
#include "profile_cache.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <filesystem/pack.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>
#include <string_utils/utils.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace unravel::deployment
{

SAVE(deploy_report_file)
{
    try_save(ar, ser20::make_nvp("path", obj.path));
    try_save(ar, ser20::make_nvp("category", obj.category));
    try_save(ar, ser20::make_nvp("bytes", obj.bytes));
}

LOAD(deploy_report_file)
{
    try_load(ar, ser20::make_nvp("path", obj.path));
    try_load(ar, ser20::make_nvp("category", obj.category));
    try_load(ar, ser20::make_nvp("bytes", obj.bytes));
}

SAVE(deploy_report_step)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("seconds", obj.seconds));
}

LOAD(deploy_report_step)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("seconds", obj.seconds));
}

namespace
{

constexpr const char* DEPLOY_REPORT_FILE = "deploy_report.json";
constexpr const char* PREVIOUS_DEPLOY_REPORT_FILE = "deploy_report_previous.json";
/// The report format this engine writes.
constexpr std::uint32_t DEPLOY_REPORT_VERSION = 1;
/// The extension of an asset pack a build holds.
constexpr const char* PACK_EXTENSION = ".pak";
/// The extension of a compiled asset file.
constexpr const char* COMPILED_EXTENSION = ".asset";

/// The category of a compiled asset, by its source's extension (".png").
auto get_asset_category(const std::string& extension) -> std::string
{
    if(ex::is_format<gfx::texture>(extension))
    {
        return "Textures";
    }
    if(ex::is_format<audio_clip>(extension))
    {
        return "Audio";
    }
    if(ex::is_format<mesh>(extension))
    {
        return "Meshes";
    }
    if(ex::is_format<material>(extension) || ex::is_format<physics_material>(extension))
    {
        return "Materials";
    }
    if(ex::is_format<animation_clip>(extension) || ex::is_format<animation_controller>(extension))
    {
        return "Animations";
    }
    if(ex::is_format<scene_prefab>(extension) || ex::is_format<prefab>(extension))
    {
        return "Scenes and Prefabs";
    }
    if(ex::is_format<font>(extension))
    {
        return "Fonts";
    }
    if(ex::is_format<gfx::shader>(extension))
    {
        return "Shaders";
    }
    if(ex::is_format<ui_tree>(extension) || ex::is_format<style_sheet>(extension))
    {
        return "UI";
    }
    return "Other Assets";
}

auto get_signed_difference(std::uint64_t newer, std::uint64_t older) -> std::int64_t
{
    return static_cast<std::int64_t>(newer) - static_cast<std::int64_t>(older);
}

/// Changes from `older` to `newer`, by key, largest first; keys only one side has count from zero.
auto diff_by_key(const std::map<std::string, std::uint64_t>& older, const std::map<std::string, std::uint64_t>& newer)
    -> std::vector<std::pair<std::string, std::int64_t>>
{
    std::vector<std::pair<std::string, std::int64_t>> changes;
    for(const auto& [key, bytes] : newer)
    {
        const auto found = older.find(key);
        const auto difference = get_signed_difference(bytes, found != older.end() ? found->second : 0);
        if(difference != 0)
        {
            changes.emplace_back(key, difference);
        }
    }
    for(const auto& [key, bytes] : older)
    {
        if(newer.count(key) == 0)
        {
            changes.emplace_back(key, -static_cast<std::int64_t>(bytes));
        }
    }
    std::stable_sort(changes.begin(),
                     changes.end(),
                     [](const auto& lhs, const auto& rhs)
                     {
                         return std::llabs(lhs.second) > std::llabs(rhs.second);
                     });
    return changes;
}

} // namespace

auto deploy_report::get_category_totals() const -> std::vector<std::pair<std::string, std::uint64_t>>
{
    std::map<std::string, std::uint64_t> totals;
    for(const auto& file : files)
    {
        totals[file.category] += file.bytes;
    }
    std::vector<std::pair<std::string, std::uint64_t>> sorted(totals.begin(), totals.end());
    std::stable_sort(sorted.begin(),
                     sorted.end(),
                     [](const auto& lhs, const auto& rhs)
                     {
                         return lhs.second > rhs.second;
                     });
    return sorted;
}

void deploy_step_times::add(const std::string& name, double seconds)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    steps_.push_back({name, seconds});
}

auto deploy_step_times::get() const -> std::vector<deploy_report_step>
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return steps_;
}

auto get_deploy_file_category(const std::string& path) -> std::string
{
    const auto name = string_utils::to_lower(fs::path(path).filename().generic_string());
    const auto compiled = name.rfind(COMPILED_EXTENSION);
    if(compiled != std::string::npos)
    {
        return get_asset_category(fs::path(name.substr(0, compiled)).extension().generic_string());
    }
    const auto extension = fs::path(name).extension().generic_string();
    if(name.find("-script.") != std::string::npos)
    {
        return "Scripts";
    }
    if(extension == ".cfg")
    {
        return "Settings";
    }
    if(extension == ".dll" || extension == ".so" || extension == ".exe" || extension == ".dylib" || extension == ".wasm" ||
       extension == ".js" || extension == ".pdb" || extension == ".json" || extension == ".dat")
    {
        return "Runtime";
    }
    return "Other";
}

auto list_deploy_files(const fs::path& data_root) -> std::vector<deploy_report_file>
{
    std::vector<deploy_report_file> files;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(data_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if(!it->is_regular_file(ec))
        {
            continue;
        }
        const auto relative = fs::relative(it->path(), data_root, ec).generic_string();
        if(it->path().extension() == PACK_EXTENSION)
        {
            fs::pack_layout layout;
            std::string error;
            if(fs::read_pack_layout(it->path(), layout, error))
            {
                for(const auto& entry : layout.entries)
                {
                    files.push_back({relative + ":" + entry.name, get_deploy_file_category(entry.name), entry.size});
                }
                continue;
            }
        }
        files.push_back({relative, get_deploy_file_category(relative), static_cast<std::uint64_t>(it->file_size(ec))});
    }
    std::sort(files.begin(),
              files.end(),
              [](const deploy_report_file& lhs, const deploy_report_file& rhs)
              {
                  return lhs.path < rhs.path;
              });
    return files;
}

auto measure_build_bytes(const fs::path& folder) -> std::uint64_t
{
    fs::error_code ec;
    if(fs::is_regular_file(folder, ec))
    {
        return static_cast<std::uint64_t>(fs::file_size(folder, ec));
    }
    std::uint64_t bytes = 0;
    for(fs::recursive_directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            bytes += static_cast<std::uint64_t>(it->file_size(ec));
        }
    }
    return bytes;
}

auto compare_deploy_reports(const deploy_report& older, const deploy_report& newer) -> deploy_report_change
{
    deploy_report_change change;
    change.build_bytes = get_signed_difference(newer.build_bytes, older.build_bytes);
    std::map<std::string, std::uint64_t> older_categories;
    std::map<std::string, std::uint64_t> newer_categories;
    std::map<std::string, std::uint64_t> older_files;
    std::map<std::string, std::uint64_t> newer_files;
    for(const auto& file : older.files)
    {
        older_categories[file.category] += file.bytes;
        older_files[file.path] = file.bytes;
    }
    for(const auto& file : newer.files)
    {
        newer_categories[file.category] += file.bytes;
        newer_files[file.path] = file.bytes;
    }
    change.categories = diff_by_key(older_categories, newer_categories);
    change.files = diff_by_key(older_files, newer_files);
    return change;
}

auto get_deploy_report_path(const fs::path& project_root, const std::string& profile) -> fs::path
{
    return get_profile_cache_folder(project_root, profile) / DEPLOY_REPORT_FILE;
}

auto get_previous_deploy_report_path(const fs::path& project_root, const std::string& profile) -> fs::path
{
    return get_profile_cache_folder(project_root, profile) / PREVIOUS_DEPLOY_REPORT_FILE;
}

auto write_deploy_report(const deploy_report& report) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("version", DEPLOY_REPORT_VERSION));
        try_save(ar, ser20::make_nvp("profile", report.profile));
        try_save(ar, ser20::make_nvp("platform", report.platform));
        try_save(ar, ser20::make_nvp("engine_version", report.engine_version));
        try_save(ar, ser20::make_nvp("finished_at", report.finished_at));
        try_save(ar, ser20::make_nvp("build_bytes", report.build_bytes));
        try_save(ar, ser20::make_nvp("steps", report.steps));
        try_save(ar, ser20::make_nvp("files", report.files));
    }
    return stream.str();
}

auto read_deploy_report(const std::string& text, deploy_report& report, std::string& error) -> bool
{
    deploy_report read;
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        std::uint32_t version{};
        if(!try_load(ar, ser20::make_nvp("version", version)) || version > DEPLOY_REPORT_VERSION)
        {
            error = "The deploy report is of a format this engine does not read.";
            return false;
        }
        try_load(ar, ser20::make_nvp("profile", read.profile));
        try_load(ar, ser20::make_nvp("platform", read.platform));
        try_load(ar, ser20::make_nvp("engine_version", read.engine_version));
        try_load(ar, ser20::make_nvp("finished_at", read.finished_at));
        try_load(ar, ser20::make_nvp("build_bytes", read.build_bytes));
        try_load(ar, ser20::make_nvp("steps", read.steps));
        try_load(ar, ser20::make_nvp("files", read.files));
    }
    catch(const std::exception& e)
    {
        error = std::string("The deploy report could not be read: ") + e.what();
        return false;
    }
    report = std::move(read);
    return true;
}

auto load_deploy_report(const fs::path& file, deploy_report& report, std::string& error) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        error = "The deploy report could not be opened.";
        return false;
    }
    return read_deploy_report(fs::read_stream_str(stream), report, error);
}

auto save_deploy_report(const fs::path& project_root, const deploy_report& report) -> bool
{
    const auto current = get_deploy_report_path(project_root, report.profile);
    const auto previous = get_previous_deploy_report_path(project_root, report.profile);
    fs::error_code ec;
    fs::create_directories(current.parent_path(), ec);
    if(fs::exists(current, ec))
    {
        fs::copy_file(current, previous, fs::copy_options::overwrite_existing, ec);
    }
    const auto text = write_deploy_report(report);
    bool is_written = false;
    asset_writer::atomic_write_file(
        current,
        [&](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), std::streamsize(text.size()));
            stream.close();
            is_written = stream.good();
        },
        ec);
    return is_written && !ec;
}

} // namespace unravel::deployment
