#pragma once

#include <context/context.hpp>
#include <deploy/deploy_settings.h>
#include <deploy/result.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/// The open project's kept releases (deploy/releases.h over the project's folder and settings).
namespace unravel::deployment
{

/// The name a kept release gets by default: the product version.
auto get_default_release_name(rtti::context& ctx) -> std::string;
auto list_project_releases() -> std::vector<std::string>;
auto get_project_release_folder(const std::string& name) -> fs::path;

/// Keeps the full packed build in the deploy folder as release `name`.
auto keep_project_release(const deploy_settings& params, const std::string& name) -> result;

/// Deletes a kept release. A project patching against it goes back to full builds (saved), since a patch
/// can no longer be made for it.
auto remove_project_release(rtti::context& ctx, const std::string& name) -> result;

} // namespace unravel::deployment
