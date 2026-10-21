#pragma once

#include <context/context.hpp>
#include <engine/settings/target_platform.h>
#include <serialization/document_paths.h>
#include <ser20/external/simdjson/simdjson.h>

#include <hpp/string_view.hpp>

#include <string>
#include <vector>

namespace unravel
{
class project_manager;
}

/// How the settings profile tools name profiles, platforms and settings paths.
namespace unravel::mcp
{

/// A platform's name in tool arguments and results: "desktop" or "android".
auto get_platform_name(target_platform platform) -> const char*;
/// The platform `name` names; false for a name that names none.
auto parse_platform_name(hpp::string_view name, target_platform& platform) -> bool;

/// A dotted settings path ("graphics.particles") as the profiles name it ("graphics/particles").
auto to_profile_path(hpp::string_view dotted) -> std::string;

/// A profile's override paths as dotted settings paths.
auto to_dotted_paths(const serialization::path_set& paths) -> std::vector<std::string>;

/// The open project, when its settings may change now: the game does not play and no deploy runs.
auto get_editable_project(rtti::context& ctx, std::string& error) -> project_manager*;

/**
 * @brief Reads the profile the arguments name at `key`, checked against the project's profiles.
 * @param profile Keeps its value when the arguments name no profile.
 * @return False, with `error` set, for a name that is no profile.
 */
auto read_profile_argument(const project_manager& pm,
                           const simdjson::dom::object& args,
                           const char* key,
                           std::string& profile,
                           std::string& error) -> bool;

/// The names the deploy tools take for the deploy settings (run_after_deploy, pack_assets, ...), in order.
auto get_deploy_argument_names() -> std::vector<std::string>;

/// The stored name of the deploy setting a deploy tool argument names ("deploy_and_run" for
/// "run_after_deploy"); empty for a name that names none.
auto find_deploy_setting_path(hpp::string_view argument) -> std::string;

/// A profile's deploy override paths as the deploy tools name the settings.
auto to_deploy_argument_names(const serialization::path_set& paths) -> std::vector<std::string>;

} // namespace unravel::mcp
