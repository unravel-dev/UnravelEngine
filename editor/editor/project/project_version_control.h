#pragma once

#include <cstdint>

namespace unravel
{
/// The version control a new project is set up for.
enum class project_version_control : std::uint8_t
{
    none,
    /// Writes a .gitignore file; the repository itself stays the user's to create.
    git,
};
} // namespace unravel
