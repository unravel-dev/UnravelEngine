#include "extras.h"
#include "files.h"
#include "folders.h"

#include <filesystem/mirror.h>
#include <logging/logging.h>

#include <string_utils/utils.h>

#include <algorithm>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
constexpr const char* FOLDER_VARIABLE = "UNRAVEL_DEPLOY_FOLDER";
constexpr const char* PROFILE_VARIABLE = "UNRAVEL_DEPLOY_PROFILE";
constexpr const char* PLATFORM_VARIABLE = "UNRAVEL_DEPLOY_PLATFORM";
constexpr const char* VERSION_VARIABLE = "UNRAVEL_DEPLOY_VERSION";

/// What ends a line of a record file.
constexpr const char* LINE_ENDS = "\r\n";

/// The names a record file lists; none for a missing file.
auto read_record(const fs::path& record) -> std::vector<std::string>
{
    std::string text;
    read_whole_file(record, text);
    return string_utils::tokenize(text, LINE_ENDS);
}

void write_record(const fs::path& record, const std::vector<std::string>& names)
{
    std::string text;
    for(const auto& name : names)
    {
        text += name + "\n";
    }
    fs::error_code ec;
    fs::create_directories(record.parent_path(), ec);
    write_text_or_throw(record, text);
}
} // namespace

auto list_extra_files(const fs::path& folder) -> std::vector<std::string>
{
    std::vector<std::string> names;
    if(folder.empty() || !is_present(folder))
    {
        return names;
    }
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            names.push_back(fs::to_utf8_string(it->path().lexically_relative(folder)));
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", folder.generic_string(), ec.message()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

void remove_stale_extra_files(const fs::path& folder, const fs::path& build, const fs::path& record)
{
    fs::error_code ec;
    fs::remove_unlisted_files(build, read_record(record), list_extra_files(folder), ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("Removing an extra file an earlier deploy copied into {} failed: {}",
                                             build.generic_string(),
                                             ec.message()));
    }
}

auto deploy_extra_files(const fs::path& folder, const fs::path& build, const fs::path& record)
    -> std::vector<std::string>
{
    // The record gains this deploy's names before the copies: a deploy that stops half way still knows every file
    // it may have copied. Once all are copied it lists them alone: the files the earlier names stood for went when
    // the deploy began (remove_stale_extra_files).
    const auto names = list_extra_files(folder);
    auto recorded = read_record(record);
    recorded.insert(recorded.end(), names.begin(), names.end());
    write_record(record, recorded);
    for(const auto& name : names)
    {
        const auto relative = fs::from_utf8_string(name);
        copy_file_or_throw(folder / relative, build / relative);
    }
    write_record(record, names);
    return names;
}

auto make_post_command_environment(const post_command_context& context) -> std::vector<std::string>
{
    return {std::string(FOLDER_VARIABLE) + "=" + fs::to_utf8_string(fs::path(context.folder).make_preferred()),
            std::string(PROFILE_VARIABLE) + "=" + context.profile,
            std::string(PLATFORM_VARIABLE) + "=" + context.platform,
            std::string(VERSION_VARIABLE) + "=" + context.version};
}

#ifdef _WIN32

auto get_post_command_script_name() -> std::string
{
    return "post_deploy.cmd";
}

auto make_post_command_script(const std::string& command) -> std::string
{
    // The script is UTF-8, which the interpreter reads once its code page says so.
    return fmt::format("@echo off\r\nchcp 65001 >nul\r\ncd /d \"%{}%\" || exit /b 1\r\n{}\r\n",
                       FOLDER_VARIABLE,
                       command);
}

auto make_post_command_arguments(const fs::path& script) -> std::vector<std::string>
{
    return {"cmd.exe", "/d", "/c", fs::to_utf8_string(fs::path(script).make_preferred())};
}

#else

auto get_post_command_script_name() -> std::string
{
    return "post_deploy.sh";
}

auto make_post_command_script(const std::string& command) -> std::string
{
    return fmt::format("cd \"${}\" || exit 1\n{}\n", FOLDER_VARIABLE, command);
}

auto make_post_command_arguments(const fs::path& script) -> std::vector<std::string>
{
    return {"/bin/sh", fs::to_utf8_string(script)};
}

#endif

} // namespace unravel::deployment
