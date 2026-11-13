#pragma once

#include <context/context.hpp>
#include <packages/package_export.h>
#include <packages/package_import.h>
#include <packages/package_install.h>
#include <packages/package_library.h>

#include <string>
#include <vector>

/// What the editor does with content packages: the open project as a package import sees it, the trash an install
/// puts files aside in, and the .packages folder packages are exported to and listed from.
namespace unravel::package_actions
{

/// The open project's .packages folder: exports go there and the Packages window lists it. It may not exist yet.
auto get_packages_folder() -> fs::path;

/// The open project as an import sees it: its assets, scripts, engine and installed packages. Main thread.
auto make_project_view(rtti::context& ctx) -> packages::project_view;

/// What an export of the open project reads. Main thread.
auto make_export_source(rtti::context& ctx) -> packages::export_source;

/// The project's installed packages. @p err says why packages.cfg did not read (logged too); empty when it did.
auto load_registry(rtti::context& ctx, std::string& err) -> packages::package_registry;

/// Installs a planned import with the watcher paused, then tells the watcher what changed. Main thread.
auto install(rtti::context& ctx,
             const packages::package_archive& archive,
             const packages::import_plan& plan,
             const packages::project_view& project) -> packages::install_result;

/// Removes an installed package, its unchanged files to the trash. Main thread.
auto remove(rtti::context& ctx, const packages::removal_plan& plan, const packages::project_view& project)
    -> packages::install_result;

/// Saves the package.json that makes the folder `root` (below the data folder) a package's source.
auto save_definition(rtti::context& ctx, const std::string& root, const packages::package_manifest& definition, std::string& err)
    -> bool;

/// Whether a file is a package file, by its extension.
auto is_package_file(const fs::path& file) -> bool;

} // namespace unravel::package_actions
