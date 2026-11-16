#pragma once

#include <https/github_links.h>

/// Where the engine lives online: shown by the start page and the About window.
namespace unravel::engine_links
{
inline constexpr const char* ORGANIZATION = UNRAVEL_GITHUB_ACCOUNT_URL;
inline constexpr const char* REPOSITORY = UNRAVEL_GITHUB_REPOSITORY_URL;
inline constexpr const char* ISSUES = UNRAVEL_GITHUB_REPOSITORY_URL "/issues";
inline constexpr const char* ENGINE_API_DOCS = "https://unravel-dev.github.io/UnravelEngine/engine-api/html/";
inline constexpr const char* SCRIPTING_API_DOCS = "https://unravel-dev.github.io/UnravelEngine/script-api/html/";
} // namespace unravel::engine_links
