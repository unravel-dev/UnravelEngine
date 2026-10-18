#pragma once

#include <filesystem/filesystem.h>

/// Checks on the folder a deploy writes into.
namespace unravel::deployment
{

/// Whether `folder` holds a build an earlier deploy wrote: every deploy writes the app's asset index,
/// loose or inside its pack.
auto is_previous_deploy(const fs::path& folder) -> bool;

/// Whether `path` is `folder` or lies inside it. Canonical paths, so "..", links and letter case cannot
/// slip past; works for paths that do not exist yet.
auto is_inside(const fs::path& path, const fs::path& folder) -> bool;

/// Whether a running program holds `executable` (Windows locks a running executable against writes).
/// Always false where the platform does not lock it, since replacing it there is safe.
auto is_executable_in_use(const fs::path& executable) -> bool;

} // namespace unravel::deployment
