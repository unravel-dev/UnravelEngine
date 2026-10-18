#pragma once

#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/// The libraries a deployed player needs beside it.
namespace unravel::deployment
{

/**
 * @brief The libraries `executable` loads from its own folder, as the platform's dependency scanner lists
 * them: `scanner` -modules on Windows (Dependencies.exe), ldd elsewhere (`scanner` unused).
 *
 * A scan takes seconds, so it is kept for the process until the executable or one of the libraries it
 * found changes size or write time. Throws when the scanner does not run or reports a failure: shipping a
 * build without its libraries would delete them from the deploy folder.
 */
auto find_dependencies(const fs::path& executable, const fs::path& scanner) -> std::vector<std::string>;

/// The scanner's output parsed: the libraries it lists from `folder`, one path each.
auto parse_dependencies(const std::string& output, const fs::path& folder) -> std::vector<std::string>;

} // namespace unravel::deployment
