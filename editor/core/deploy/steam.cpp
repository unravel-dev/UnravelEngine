#include "steam.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <hpp/string_view.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace unravel::deployment
{
namespace
{
/// The files a game run with --log-here writes beside its executable (engine::open_log, the crash handler).
constexpr std::array<const char*, 3> EXCLUDED_FILES = {"Log.txt", "CrashLog.txt", "CrashLog.dmp"};
/// What steamcmd prints when a build is committed: "Successfully finished AppID 480 build (BuildID 123)."
constexpr hpp::string_view FINISHED_BUILD_TEXT = "Successfully finished AppID";
constexpr hpp::string_view BUILD_ID_TEXT = "BuildID ";
/// Lower case parts of what steamcmd prints when the account cannot log in.
constexpr std::array<hpp::string_view, 6> LOGIN_FAILURE_TEXTS = {"login failure",
                                                                 "failed to log",
                                                                 "cached credentials not found",
                                                                 "password",
                                                                 "steam guard",
                                                                 "two-factor"};
constexpr char SCRIPT_INDENT = '\t';

/// Whether a name can stand in a steamcmd command line and in the script: no spaces, quotes or control
/// characters.
auto is_plain_name(const std::string& name) -> bool
{
    return std::none_of(name.begin(),
                        name.end(),
                        [](char c)
                        {
                            const auto byte = static_cast<unsigned char>(c);
                            return c == '"' || std::isspace(byte) != 0 || std::iscntrl(byte) != 0;
                        });
}

/// Text inside a quoted script value: quotes become apostrophes and line breaks spaces.
auto to_script_text(const std::string& text) -> std::string
{
    std::string result = text;
    std::replace(result.begin(), result.end(), '"', '\'');
    std::replace_if(
        result.begin(),
        result.end(),
        [](char c)
        {
            return std::iscntrl(static_cast<unsigned char>(c)) != 0;
        },
        ' ');
    return result;
}

/// A folder as the script names it: forward slashes, which a backslash escape cannot be mistaken for, and
/// a trailing one.
auto to_script_folder(const fs::path& folder) -> std::string
{
    auto text = fs::to_utf8_string(folder.lexically_normal());
    std::replace(text.begin(), text.end(), '\\', '/');
    return !text.empty() && text.back() == '/' ? text : text + '/';
}

void append_value(std::string& script, int depth, const std::string& key, const std::string& value)
{
    script.append(static_cast<std::size_t>(depth), SCRIPT_INDENT);
    script += '"' + key + "\" \"" + value + "\"\n";
}

void open_block(std::string& script, int depth, const std::string& key)
{
    script.append(static_cast<std::size_t>(depth), SCRIPT_INDENT);
    script += '"' + key + "\"\n";
    script.append(static_cast<std::size_t>(depth), SCRIPT_INDENT);
    script += "{\n";
}

void close_block(std::string& script, int depth)
{
    script.append(static_cast<std::size_t>(depth), SCRIPT_INDENT);
    script += "}\n";
}
} // namespace

auto check_steam_settings(const steam_deploy_settings& steam) -> std::string
{
    if(steam.app_id == 0)
    {
        return "Set the profile's Steam app ID.";
    }
    if(steam.depot_id == 0)
    {
        return "Set the profile's Steam depot ID.";
    }
    if(steam.account.empty())
    {
        return "Set the Steam account that uploads.";
    }
    if(!is_plain_name(steam.account))
    {
        return fmt::format("The Steam account '{}' has spaces or quotes, which no account name has.", steam.account);
    }
    if(string_utils::to_lower(steam.branch) == STEAM_DEFAULT_BRANCH)
    {
        return "Steam sets a build live on the default branch only on the Steamworks website. Name a beta branch, "
               "or leave Set Live On empty.";
    }
    if(!is_plain_name(steam.branch))
    {
        return fmt::format("The branch '{}' has spaces or quotes, which no branch name has.", steam.branch);
    }
    return {};
}

auto make_steam_app_build_script(const steam_build& build) -> std::string
{
    std::string script;
    open_block(script, 0, "AppBuild");
    append_value(script, 1, "AppID", std::to_string(build.app_id));
    append_value(script, 1, "Desc", to_script_text(build.description));
    append_value(script, 1, "ContentRoot", to_script_folder(build.content_root));
    append_value(script, 1, "BuildOutput", to_script_folder(build.build_output));
    if(!build.branch.empty())
    {
        append_value(script, 1, "SetLive", build.branch);
    }
    open_block(script, 1, "Depots");
    open_block(script, 2, std::to_string(build.depot_id));
    open_block(script, 3, "FileMapping");
    append_value(script, 4, "LocalPath", "*");
    append_value(script, 4, "DepotPath", ".");
    append_value(script, 4, "recursive", "1");
    close_block(script, 3);
    for(const char* name : EXCLUDED_FILES)
    {
        append_value(script, 3, "FileExclusion", name);
    }
    close_block(script, 2);
    close_block(script, 1);
    close_block(script, 0);
    return script;
}

auto make_steamcmd_arguments(const fs::path& steamcmd, const std::string& account, const fs::path& script)
    -> std::vector<std::string>
{
    return {steamcmd.empty() ? std::string(DEFAULT_STEAMCMD) : fs::to_utf8_string(steamcmd),
            "+@ShutdownOnFailedCommand",
            "1",
            "+@NoPromptForPassword",
            "1",
            "+login",
            account,
            "+run_app_build",
            fs::to_utf8_string(script),
            "+quit"};
}

auto find_steam_build_id(const std::string& output) -> std::string
{
    const auto finished = output.find(FINISHED_BUILD_TEXT.data(), 0, FINISHED_BUILD_TEXT.size());
    if(finished == std::string::npos)
    {
        return {};
    }
    const auto label = output.find(BUILD_ID_TEXT.data(), finished, BUILD_ID_TEXT.size());
    if(label == std::string::npos)
    {
        return {};
    }
    const auto begin = output.begin() + static_cast<std::ptrdiff_t>(label + BUILD_ID_TEXT.size());
    const auto end = std::find_if(begin,
                                  output.end(),
                                  [](char c)
                                  {
                                      return std::isdigit(static_cast<unsigned char>(c)) == 0;
                                  });
    return {begin, end};
}

auto is_steam_login_failure(const std::string& output) -> bool
{
    const auto lower = string_utils::to_lower(output);
    return std::any_of(LOGIN_FAILURE_TEXTS.begin(),
                       LOGIN_FAILURE_TEXTS.end(),
                       [&lower](hpp::string_view text)
                       {
                           return lower.find(text.data(), 0, text.size()) != std::string::npos;
                       });
}

} // namespace unravel::deployment
