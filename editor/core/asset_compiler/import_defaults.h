#pragma once

#include <filesystem/filesystem.h>

/// Which import defaults an asset compiles with in the editor: the project's for the project's own assets, a new
/// project's for the engine's and the editor's, which every project shares.
namespace unravel::asset_compiler
{

/// Whether the asset at `key` (a protocol or absolute path) is the project's (app:), and follows its import defaults.
auto follows_project_defaults(const fs::path& key) -> bool;

} // namespace unravel::asset_compiler
